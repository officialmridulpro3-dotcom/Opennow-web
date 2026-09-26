//! GFN-style in-window overlay for standalone Windows sessions.
//!
//! Ctrl+G opens a sidebar menu and Ctrl+N a live statistics strip. Both are
//! borderless always-on-top SDL windows painted with GDI onto layered
//! surfaces; the stats strip is additionally click-through so the game keeps
//! receiving every mouse event beneath it. Video keeps presenting on the
//! backend thread while either panel is visible.
//!
//! Live network numbers come from the transport's shared feedback state,
//! published once per session into a process slot; video geometry is
//! configured once from the stream config at output initialization.

use std::ffi::c_void;
use std::ffi::OsStr;
use std::mem::{size_of, zeroed};
use std::os::windows::ffi::OsStrExt;
use std::ptr::null_mut;
use std::time::{Duration, Instant};

use opennow_streamer_transport::{OverlayVideoCounters, session_feedback};
use raw_window_handle::{HasWindowHandle, RawWindowHandle};
use sdl2::event::{Event, WindowEvent};
use sdl2::keyboard::{Mod, Scancode};
use sdl2::mouse::MouseButton;
use sdl2::video::Window;
use windows_sys::Win32::Foundation::{HWND, POINT, RECT, SIZE};
use windows_sys::Win32::Graphics::Gdi::{
    AC_SRC_ALPHA, AC_SRC_OVER, ANTIALIASED_QUALITY, BI_RGB, BITMAPINFO, BITMAPINFOHEADER,
    BLENDFUNCTION, CreateCompatibleDC, CreateDIBSection, CreateFontW, CreateSolidBrush, DeleteDC,
    DeleteObject, DIB_RGB_COLORS, FW_BOLD, FW_NORMAL, GetStockObject, HBITMAP, HBRUSH, HDC, HFONT,
    NULL_BRUSH, SelectObject, SetBkMode, SetTextColor, TRANSPARENT,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    DrawTextW, DT_END_ELLIPSIS, DT_LEFT, DT_RIGHT, DT_SINGLELINE, DT_VCENTER, FillRect, FrameRect,
    GetCursorPos, GetDC, GetWindowLongPtrW, GetWindowRect, GWL_EXSTYLE, HWND_TOPMOST, ReleaseDC,
    SetForegroundWindow, SetWindowLongPtrW, SetWindowPos, ShowWindow, SW_HIDE, SW_SHOW, SW_SHOWNA,
    SWP_NOACTIVATE, SWP_NOMOVE, SWP_NOSIZE, ULW_ALPHA, UpdateLayeredWindow, WS_EX_LAYERED,
    WS_EX_NOACTIVATE, WS_EX_TRANSPARENT,
};

/// Menu selections the overlay hands back to the game surface for execution.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum OverlayAction {
    CloseMenu,
    ToggleFullscreen,
    ToggleStats,
    Quit,
}

const MENU_ROWS: usize = 4;
const MENU_RESUME: usize = 0;
const MENU_FULLSCREEN: usize = 1;
const MENU_STATISTICS: usize = 2;

const COLOR_BG: u32 = 0x001c1714;
const COLOR_HOVER: u32 = 0x00332a23;
const COLOR_BORDER: u32 = 0x003d332c;
const COLOR_ACCENT: u32 = 0x0000b976;
const COLOR_TEXT: u32 = 0x00edeae8;
const COLOR_DIM: u32 = 0x00a6a09a;
const COLOR_OFF: u32 = 0x0080726b;

const MENU_WIDTH: i32 = 340;
const MENU_HEIGHT: i32 = 316;
const MENU_ROWS_TOP: i32 = 72;
const MENU_ROW_H: i32 = 52;
const STATS_WIDTH: i32 = 360;
const STATS_HEIGHT: i32 = 246;
const STATS_ROWS_TOP: i32 = 36;
const STATS_ROW_H: i32 = 30;
const STATS_ROWS: usize = 6;
const PANEL_ALPHA: u8 = 232;

pub(crate) struct OverlayManager {
    menu: LayeredPanel,
    stats: LayeredPanel,
    game_id: u32,
    menu_open: bool,
    stats_open: bool,
    stats_enabled: bool,
    selected: usize,
    menu_hovered: bool,
    menu_focused: bool,
    game_focused: bool,
    fullscreen: bool,
    video_desc: String,
    decoder_label: String,
    last_sample: Option<(Instant, OverlayVideoCounters)>,
    last_stats_paint: Instant,
    last_reposition: Instant,
}

impl OverlayManager {
    pub(crate) fn new(video: &sdl2::VideoSubsystem, game_id: u32) -> Result<Self, String> {
        let menu = LayeredPanel::new(video, "OpenNOW Stream Menu", MENU_WIDTH, MENU_HEIGHT, false)?;
        let stats =
            LayeredPanel::new(video, "OpenNOW Stream Stats", STATS_WIDTH, STATS_HEIGHT, true)?;
        Ok(Self {
            menu,
            stats,
            game_id,
            menu_open: false,
            stats_open: false,
            stats_enabled: false,
            selected: 0,
            menu_hovered: false,
            menu_focused: false,
            game_focused: false,
            fullscreen: false,
            video_desc: String::from("\u{2014}"),
            decoder_label: String::from("\u{2014}"),
            last_sample: None,
            last_stats_paint: Instant::now(),
            last_reposition: Instant::now(),
        })
    }

    pub(crate) fn set_video_info(&mut self, video_desc: String, decoder_label: &'static str) {
        self.video_desc = video_desc;
        self.decoder_label = decoder_label.to_owned();
    }

    pub(crate) fn set_context(&mut self, fullscreen: bool) {
        if self.fullscreen != fullscreen {
            self.fullscreen = fullscreen;
            if self.menu_open {
                self.paint_menu();
            }
        }
    }

    pub(crate) fn menu_is_open(&self) -> bool {
        self.menu_open
    }

    pub(crate) fn show_menu(&mut self, game: &Window) {
        eprintln!("Windows SDL overlay: menu opened (Ctrl+G)");
        self.stats_open = false;
        self.stats.hide();
        self.selected = 0;
        self.menu_open = true;
        self.position_panels(game);
        self.paint_menu();
        self.menu.show(true);
        unsafe {
            SetForegroundWindow(self.menu.hwnd);
        }
        // The cursor may already sit inside the fresh panel without crossing
        // its edge, which would produce no Enter event: hit-test once.
        self.menu_hovered = cursor_inside(self.menu.rect());
    }

    pub(crate) fn hide_menu(&mut self, game: &Window, refocus_game: bool) {
        if !self.menu_open {
            return;
        }
        eprintln!("Windows SDL overlay: menu closed");
        self.menu_open = false;
        self.menu_focused = false;
        self.menu_hovered = false;
        self.menu.hide();
        if refocus_game {
            if let Ok(hwnd) = window_hwnd(game) {
                unsafe {
                    SetForegroundWindow(hwnd);
                }
            }
        }
        if self.stats_enabled && self.game_focused {
            self.show_stats(game);
        }
    }

    pub(crate) fn toggle_stats(&mut self, game: &Window) {
        self.stats_enabled = !self.stats_enabled;
        eprintln!(
            "Windows SDL overlay: statistics {}",
            if self.stats_enabled {
                "on (Ctrl+N)"
            } else {
                "off"
            }
        );
        if self.stats_enabled {
            if self.menu_open {
                // The strip returns when the menu closes.
                self.paint_menu();
            } else {
                self.show_stats(game);
            }
        } else {
            self.stats_open = false;
            self.stats.hide();
            if self.menu_open {
                self.paint_menu();
            }
        }
    }

    pub(crate) fn hide_all(&mut self) {
        self.menu_open = false;
        self.stats_open = false;
        self.menu_focused = false;
        self.menu_hovered = false;
        self.menu.hide();
        self.stats.hide();
    }

    /// Periodic refresh: repaint live stats and keep panels glued to the game.
    pub(crate) fn tick(&mut self, game: &Window) {
        if self.stats_open && self.last_stats_paint.elapsed() >= Duration::from_millis(500) {
            self.paint_stats(game);
        }
        if (self.menu_open || self.stats_open)
            && self.last_reposition.elapsed() >= Duration::from_secs(2)
        {
            self.last_reposition = Instant::now();
            self.position_panels(game);
        }
    }

    /// Route one SDL event. Returns (consumed, actions): consumed events must
    /// not reach game input.
    pub(crate) fn route_event(
        &mut self,
        game: &Window,
        event: &Event,
    ) -> (bool, Vec<OverlayAction>) {
        match event {
            Event::Window {
                window_id,
                win_event,
            } => {
                self.route_window_event(game, *window_id, win_event.clone());
                // Game-window events still belong to game input (focus drives
                // capture); overlay-window events are fully consumed.
                (
                    *window_id == self.menu.window.id() || *window_id == self.stats.window.id(),
                    Vec::new(),
                )
            }
            Event::MouseMotion { x, y, .. } => {
                if self.menu_open && self.menu_hovered {
                    self.hover_at(*x, *y);
                    (true, Vec::new())
                } else {
                    (false, Vec::new())
                }
            }
            Event::MouseButtonDown {
                mouse_btn, x, y, ..
            } => {
                if self.menu_open && self.menu_hovered && *mouse_btn == MouseButton::Left {
                    (true, self.click_at(*x, *y))
                } else {
                    (false, Vec::new())
                }
            }
            Event::MouseButtonUp { .. } | Event::MouseWheel { .. } => {
                (self.menu_open && self.menu_hovered, Vec::new())
            }
            Event::KeyDown { scancode, keymod, .. } => {
                if self.menu_open && self.menu_focused {
                    (true, self.menu_key(*scancode, *keymod))
                } else {
                    (false, Vec::new())
                }
            }
            Event::KeyUp { .. } => (self.menu_open && self.menu_focused, Vec::new()),
            _ => (false, Vec::new()),
        }
    }

    fn route_window_event(&mut self, game: &Window, window_id: u32, win_event: WindowEvent) {
        if window_id == self.game_id {
            match win_event {
                WindowEvent::FocusGained => {
                    self.game_focused = true;
                    if self.menu_open {
                        // Clicking back into the game dismisses the menu.
                        self.hide_menu(game, false);
                    } else if self.stats_enabled && !self.stats_open {
                        self.show_stats(game);
                    }
                }
                WindowEvent::FocusLost => {
                    self.game_focused = false;
                    if !self.menu_open {
                        // Focus left the app entirely: stats must not float
                        // over other programs.
                        self.stats_open = false;
                        self.stats.hide();
                    }
                }
                WindowEvent::Moved(_, _)
                | WindowEvent::Resized(_, _)
                | WindowEvent::SizeChanged(_, _) => {
                    self.position_panels(game);
                }
                _ => {}
            }
            return;
        }
        if window_id == self.menu.window.id() {
            match win_event {
                WindowEvent::FocusGained => self.menu_focused = true,
                WindowEvent::FocusLost => {
                    // Focus went to the game (dismiss path) or out of the app
                    // (Alt+Tab): never steal it back from here.
                    self.hide_menu(game, false);
                }
                WindowEvent::Enter => self.menu_hovered = true,
                WindowEvent::Leave => self.menu_hovered = false,
                _ => {}
            }
        }
        // Stats-window events are impossible (click-through + no-activate).
    }

    fn position_panels(&mut self, game: &Window) {
        let (gx, gy, gw) = game_rect(game);
        self.menu.set_position(gx + gw - MENU_WIDTH - 16, gy + 16);
        self.stats.set_position(gx + 16, gy + 16);
    }

    fn row_at(x: i32, y: i32) -> Option<usize> {
        if x < 0 || x >= MENU_WIDTH {
            return None;
        }
        let rel = y - MENU_ROWS_TOP;
        if rel < 0 {
            return None;
        }
        let row = (rel / MENU_ROW_H) as usize;
        (row < MENU_ROWS).then_some(row)
    }

    fn hover_at(&mut self, x: i32, y: i32) {
        if let Some(row) = Self::row_at(x, y) {
            if row != self.selected {
                self.selected = row;
                self.paint_menu();
            }
        }
    }

    fn click_at(&mut self, x: i32, y: i32) -> Vec<OverlayAction> {
        let Some(row) = Self::row_at(x, y) else {
            return Vec::new();
        };
        self.selected = row;
        Self::activate(row)
    }

    fn menu_key(&mut self, scancode: Option<Scancode>, keymod: Mod) -> Vec<OverlayAction> {
        match scancode {
            Some(Scancode::Escape) => vec![OverlayAction::CloseMenu],
            Some(Scancode::Up) => {
                self.selected = self.selected.checked_sub(1).unwrap_or(MENU_ROWS - 1);
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Down) => {
                self.selected = (self.selected + 1) % MENU_ROWS;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Return) | Some(Scancode::KpEnter) => Self::activate(self.selected),
            Some(Scancode::G)
                if keymod.intersects(Mod::LCTRLMOD | Mod::RCTRLMOD) =>
            {
                vec![OverlayAction::CloseMenu]
            }
            _ => Vec::new(),
        }
    }

    fn activate(row: usize) -> Vec<OverlayAction> {
        match row {
            MENU_RESUME => vec![OverlayAction::CloseMenu],
            MENU_FULLSCREEN => vec![OverlayAction::ToggleFullscreen],
            MENU_STATISTICS => vec![OverlayAction::ToggleStats],
            _ => vec![OverlayAction::Quit],
        }
    }

    fn paint_menu(&mut self) {
        let video_desc = self.video_desc.clone();
        let selected = self.selected;
        let states = [self.fullscreen, self.stats_enabled];
        self.menu.paint_frame(|dc, fonts, brushes| {
            paint_menu_frame(dc, fonts, brushes, &video_desc, selected, states);
        });
    }

    fn paint_stats(&mut self, game: &Window) {
        let lines = self.stats_lines(game);
        self.last_stats_paint = Instant::now();
        self.stats.paint_frame(|dc, fonts, brushes| {
            paint_stats_frame(dc, fonts, brushes, &lines);
        });
    }

    fn show_stats(&mut self, game: &Window) {
        self.stats_open = true;
        self.position_panels(game);
        self.paint_stats(game);
        self.stats.show(false);
    }

    fn stats_lines(&mut self, game: &Window) -> [(String, String); STATS_ROWS] {
        let now = Instant::now();
        let telemetry = session_feedback();
        let sample = telemetry
            .as_ref()
            .map(|fb| (fb.ping_ms(now), fb.overlay_counters()));
        let mut rate = String::from("warming up\u{2026}");
        if let Some((_, counters)) = sample.as_ref() {
            if let Some((prev_at, prev)) = self.last_sample {
                let dt = now.duration_since(prev_at).as_secs_f64();
                if dt >= 0.2 {
                    let fps = counters.frames.saturating_sub(prev.frames) as f64 / dt;
                    let mbps =
                        counters.bytes.saturating_sub(prev.bytes) as f64 * 8.0 / dt / 1_000_000.0;
                    rate = format!("{mbps:.1} Mbps \u{b7} {fps:.0} fps");
                }
            }
            self.last_sample = Some((now, *counters));
        }
        let (net, frames) = match sample.as_ref() {
            Some((rtt, counters)) => {
                let rtt = *rtt;
                let rtt_text =
                    rtt.map_or_else(|| String::from("\u{2014}"), |ms| format!("{ms:.0} ms"));
                let loss_text = seq_loss_percent(counters)
                    .map_or_else(|| String::from("\u{2014}"), |pct| format!("{pct:.1}%"));
                (
                    format!("RTT {rtt_text} \u{b7} loss {loss_text}"),
                    format!("{} \u{b7} rec {}", counters.frames, counters.recovered),
                )
            }
            None => (String::from("no session"), String::from("\u{2014}")),
        };
        let (gw, gh) = game.size();
        [
            (String::from("VIDEO"), self.video_desc.clone()),
            (String::from("RATE"), rate),
            (String::from("NET"), net),
            (String::from("FRAMES"), frames),
            (String::from("DECODER"), self.decoder_label.clone()),
            (String::from("WINDOW"), format!("{gw} \u{d7} {gh}")),
        ]
    }
}

fn seq_loss_percent(counters: &OverlayVideoCounters) -> Option<f64> {
    if counters.seq_base == u32::MAX || counters.seq_highest <= counters.seq_base {
        return None;
    }
    let span = counters.seq_highest - counters.seq_base + 1;
    if span < 100 {
        return None;
    }
    let lost = span.saturating_sub(counters.packets);
    Some((lost as f64 / span as f64 * 100.0).clamp(0.0, 100.0))
}

fn state_text(on: bool) -> &'static str {
    if on { "On" } else { "Off" }
}

fn paint_menu_frame(
    dc: HDC,
    fonts: &PanelFonts,
    brushes: &PanelBrushes,
    video_desc: &str,
    selected: usize,
    states: [bool; 2],
) {
    fill_rect(dc, rect(0, 0, MENU_WIDTH, MENU_HEIGHT), brushes.bg);
    draw_text(
        dc,
        fonts.bold,
        COLOR_TEXT,
        rect(20, 8, MENU_WIDTH - 40, 28),
        "OpenNOW Stream",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(20, 34, MENU_WIDTH - 40, 22),
        video_desc,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
    );
    let labels = ["Resume", "Fullscreen", "Statistics", "Quit game"];
    for (index, label) in labels.iter().enumerate() {
        let y = MENU_ROWS_TOP + index as i32 * MENU_ROW_H;
        if index == selected {
            fill_rect(dc, rect(0, y, MENU_WIDTH, MENU_ROW_H), brushes.hover);
            fill_rect(dc, rect(0, y, 4, MENU_ROW_H), brushes.accent);
        }
        draw_text(
            dc,
            fonts.regular,
            COLOR_TEXT,
            rect(20, y, MENU_WIDTH - 140, MENU_ROW_H),
            label,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        );
        let state = match index {
            MENU_FULLSCREEN => Some(states[0]),
            MENU_STATISTICS => Some(states[1]),
            _ => None,
        };
        if let Some(on) = state {
            draw_text(
                dc,
                fonts.regular,
                if on { COLOR_ACCENT } else { COLOR_OFF },
                rect(MENU_WIDTH - 120, y, 100, MENU_ROW_H),
                state_text(on),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            );
        }
    }
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(20, MENU_HEIGHT - 28, MENU_WIDTH - 40, 20),
        "Ctrl+G close \u{b7} F8 mouse lock \u{b7} \u{2191}\u{2193} + Enter",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    frame_rect(dc, rect(0, 0, MENU_WIDTH, MENU_HEIGHT), brushes.border);
    fill_rect(dc, rect(0, 0, MENU_WIDTH, 2), brushes.accent);
}

fn paint_stats_frame(
    dc: HDC,
    fonts: &PanelFonts,
    brushes: &PanelBrushes,
    lines: &[(String, String); STATS_ROWS],
) {
    fill_rect(dc, rect(0, 0, STATS_WIDTH, STATS_HEIGHT), brushes.bg);
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, 8, STATS_WIDTH - 32, 20),
        "STREAM STATISTICS",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    for (index, (label, value)) in lines.iter().enumerate() {
        let y = STATS_ROWS_TOP + index as i32 * STATS_ROW_H;
        draw_text(
            dc,
            fonts.small,
            COLOR_DIM,
            rect(16, y, 84, STATS_ROW_H),
            label,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER,
        );
        draw_text(
            dc,
            fonts.regular,
            COLOR_TEXT,
            rect(100, y, STATS_WIDTH - 116, STATS_ROW_H),
            value,
            DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
        );
    }
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, STATS_HEIGHT - 24, STATS_WIDTH - 32, 18),
        "Ctrl+N to hide",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    frame_rect(dc, rect(0, 0, STATS_WIDTH, STATS_HEIGHT), brushes.border);
    fill_rect(dc, rect(0, 0, STATS_WIDTH, 2), brushes.accent);
}

struct PanelFonts {
    regular: HFONT,
    bold: HFONT,
    small: HFONT,
}

struct PanelBrushes {
    bg: HBRUSH,
    hover: HBRUSH,
    accent: HBRUSH,
    border: HBRUSH,
}

struct LayeredPanel {
    window: Window,
    hwnd: HWND,
    width: i32,
    height: i32,
    pos_x: i32,
    pos_y: i32,
    mem_dc: HDC,
    dib: HBITMAP,
    bits: *mut u32,
    fonts: PanelFonts,
    brushes: PanelBrushes,
}

impl LayeredPanel {
    fn new(
        video: &sdl2::VideoSubsystem,
        title: &str,
        width: i32,
        height: i32,
        click_through: bool,
    ) -> Result<Self, String> {
        let window = video
            .window(title, width as u32, height as u32)
            .position(0, 0)
            .borderless()
            .hidden()
            .build()
            .map_err(|error| format!("overlay window creation failed: {error}"))?;
        let hwnd = window_hwnd(&window)?;
        unsafe {
            let mut ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE) as u32;
            ex_style |= WS_EX_LAYERED;
            if click_through {
                ex_style |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
            }
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style as isize);
            SetWindowPos(
                hwnd,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE,
            );
        }
        unsafe {
            let screen_dc = GetDC(null_mut());
            let mem_dc = CreateCompatibleDC(screen_dc);
            ReleaseDC(null_mut(), screen_dc);
            if mem_dc.is_null() {
                return Err("overlay device context failed".to_owned());
            }
            let mut bmi: BITMAPINFO = zeroed();
            bmi.bmiHeader.biSize = size_of::<BITMAPINFOHEADER>() as u32;
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            let mut bits: *mut c_void = null_mut();
            let dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
            if dib.is_null() || bits.is_null() {
                DeleteDC(mem_dc);
                return Err("overlay DIB section failed".to_owned());
            }
            SelectObject(mem_dc, dib);
            let fonts = PanelFonts {
                regular: create_font(16, FW_NORMAL)?,
                bold: create_font(19, FW_BOLD)?,
                small: create_font(13, FW_NORMAL)?,
            };
            let brushes = PanelBrushes {
                bg: CreateSolidBrush(COLOR_BG),
                hover: CreateSolidBrush(COLOR_HOVER),
                accent: CreateSolidBrush(COLOR_ACCENT),
                border: CreateSolidBrush(COLOR_BORDER),
            };
            Ok(Self {
                window,
                hwnd,
                width,
                height,
                pos_x: 0,
                pos_y: 0,
                mem_dc,
                dib,
                bits: bits as *mut u32,
                fonts,
                brushes,
            })
        }
    }

    fn show(&self, activate: bool) {
        unsafe {
            ShowWindow(self.hwnd, if activate { SW_SHOW } else { SW_SHOWNA });
        }
    }

    fn hide(&self) {
        unsafe {
            ShowWindow(self.hwnd, SW_HIDE);
        }
    }

    fn set_position(&mut self, x: i32, y: i32) {
        if self.pos_x == x && self.pos_y == y {
            return;
        }
        self.pos_x = x;
        self.pos_y = y;
        unsafe {
            SetWindowPos(
                self.hwnd,
                HWND_TOPMOST,
                x,
                y,
                0,
                0,
                SWP_NOSIZE | SWP_NOACTIVATE,
            );
        }
    }

    fn rect(&self) -> RECT {
        RECT {
            left: self.pos_x,
            top: self.pos_y,
            right: self.pos_x + self.width,
            bottom: self.pos_y + self.height,
        }
    }

    fn paint_frame(&mut self, paint: impl FnOnce(HDC, &PanelFonts, &PanelBrushes)) {
        paint(self.mem_dc, &self.fonts, &self.brushes);
        self.present();
    }

    fn present(&self) {
        unsafe {
            // GDI clears the high byte on many raster ops: force every pixel
            // opaque and let the constant blend alpha do the translucency.
            let count = (self.width * self.height) as usize;
            let pixels = std::slice::from_raw_parts_mut(self.bits, count);
            for pixel in pixels.iter_mut() {
                *pixel |= 0xff00_0000;
            }
            let mut area = RECT {
                left: 0,
                top: 0,
                right: 0,
                bottom: 0,
            };
            GetWindowRect(self.hwnd, &mut area);
            let dest = POINT {
                x: area.left,
                y: area.top,
            };
            let size = SIZE {
                cx: self.width,
                cy: self.height,
            };
            let src = POINT { x: 0, y: 0 };
            let blend = BLENDFUNCTION {
                BlendOp: AC_SRC_OVER,
                BlendFlags: 0,
                SourceConstantAlpha: PANEL_ALPHA,
                AlphaFormat: AC_SRC_ALPHA,
            };
            UpdateLayeredWindow(
                self.hwnd,
                null_mut(),
                &dest,
                &size,
                self.mem_dc,
                &src,
                0,
                &blend,
                ULW_ALPHA,
            );
        }
    }
}

impl Drop for LayeredPanel {
    fn drop(&mut self) {
        unsafe {
            SelectObject(self.mem_dc, GetStockObject(NULL_BRUSH));
            DeleteObject(self.dib);
            DeleteObject(self.fonts.regular);
            DeleteObject(self.fonts.bold);
            DeleteObject(self.fonts.small);
            DeleteObject(self.brushes.bg);
            DeleteObject(self.brushes.hover);
            DeleteObject(self.brushes.accent);
            DeleteObject(self.brushes.border);
            DeleteDC(self.mem_dc);
        }
    }
}

fn game_rect(game: &Window) -> (i32, i32, i32) {
    if let Ok(hwnd) = window_hwnd(game) {
        unsafe {
            let mut area = RECT {
                left: 0,
                top: 0,
                right: 0,
                bottom: 0,
            };
            if GetWindowRect(hwnd, &mut area) != 0 {
                return (area.left, area.top, area.right - area.left);
            }
        }
    }
    let (width, _) = game.size();
    (0, 0, width as i32)
}

fn window_hwnd(window: &Window) -> Result<HWND, String> {
    match window
        .window_handle()
        .map_err(|error| format!("SDL window handle unavailable: {error}"))?
        .as_raw()
    {
        RawWindowHandle::Win32(handle) => Ok(handle.hwnd.get() as HWND),
        _ => Err("SDL did not create a Win32 window".to_owned()),
    }
}

fn create_font(px: i32, weight: i32) -> Result<HFONT, String> {
    let face: Vec<u16> = OsStr::new("Segoe UI")
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let font = unsafe {
        CreateFontW(
            -px,
            0,
            0,
            0,
            weight,
            0,
            0,
            0,
            0,
            0,
            0,
            ANTIALIASED_QUALITY,
            0,
            face.as_ptr(),
        )
    };
    if font.is_null() {
        Err("overlay font creation failed".to_owned())
    } else {
        Ok(font)
    }
}

fn rect(x: i32, y: i32, width: i32, height: i32) -> RECT {
    RECT {
        left: x,
        top: y,
        right: x + width,
        bottom: y + height,
    }
}

fn fill_rect(dc: HDC, area: RECT, brush: HBRUSH) {
    unsafe {
        FillRect(dc, &area, brush);
    }
}

fn frame_rect(dc: HDC, area: RECT, brush: HBRUSH) {
    unsafe {
        FrameRect(dc, &area, brush);
    }
}

fn draw_text(dc: HDC, font: HFONT, color: u32, mut area: RECT, text: &str, format: u32) {
    unsafe {
        SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT);
        let wide: Vec<u16> = OsStr::new(text)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect();
        DrawTextW(dc, wide.as_ptr(), -1, &mut area, format);
    }
}

fn cursor_inside(area: RECT) -> bool {
    unsafe {
        let mut point = POINT { x: 0, y: 0 };
        if GetCursorPos(&mut point) == 0 {
            return false;
        }
        point.x >= area.left && point.x < area.right && point.y >= area.top && point.y < area.bottom
    }
}
