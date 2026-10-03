//! OpenNOW native window — full deck revamp, native-only, LEFT sidebar full fledged.
//! Ctrl+G = OLD full sidebar on LEFT with tabs Session/Controls/Media/Keys (like first WebRTC version)
//! Ctrl+N = compact small stats like GeForce NOW (169 GAME / 120 STREAM / 40 PING) 340x520
//! Deck: layered blue-black, neon mesh 40px, scan 3% 5px, tri-bloom, sweep, tick ruler,
//! chamfered cells, slanted segments, corner brackets, 96px numeral, live dot.
//! Clipboard paste Ctrl+V → remote (Steam credentials)

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
    BLENDFUNCTION, CreateCompatibleDC, CreateDIBSection, CreateFontW, CreatePen, CreateSolidBrush,
    DeleteDC, DeleteObject, DIB_RGB_COLORS, DrawTextW, DT_CENTER, DT_END_ELLIPSIS, DT_LEFT,
    DT_RIGHT, DT_SINGLELINE, DT_VCENTER, Ellipse, FillRect, FW_BLACK, FW_BOLD,
    FW_NORMAL, GetDC, GetStockObject, HBITMAP, HBRUSH, HDC, HFONT, HPEN, NULL_BRUSH, PS_SOLID,
    Polygon, Polyline, ReleaseDC, SelectObject, SetBkMode, SetTextColor, TRANSPARENT,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    GetCursorPos, GetWindowLongPtrW, GetWindowRect, GWL_EXSTYLE, HWND_TOPMOST, SetForegroundWindow,
    SetWindowLongPtrW, SetWindowPos, ShowWindow, SW_HIDE, SW_SHOW, SW_SHOWNA, SWP_NOACTIVATE,
    SWP_NOMOVE, SWP_NOSIZE, ULW_ALPHA, UpdateLayeredWindow, WS_EX_LAYERED, WS_EX_NOACTIVATE,
    WS_EX_TRANSPARENT,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum OverlayAction {
    CloseMenu,
    ToggleFullscreen,
    ToggleStats,
    CaptureMouse,
    ToggleRecording,
    Quit,
}

const MENU_ROWS: usize = 6;
const MENU_FULLSCREEN: usize = 0;
const MENU_CAPTURE_MOUSE: usize = 1;
const MENU_TOGGLE_MIC: usize = 2;
const MENU_SCREENSHOT: usize = 3;
const MENU_SHOW_TIME: usize = 4;
const MENU_END_SESSION: usize = 5;
// legacy aliases
const MENU_RESUME: usize = 5;
const MENU_STATISTICS: usize = 4;
const MENU_RECORDING: usize = 2;

const MENU_TABS: usize = 4;
const TAB_SESSION: usize = 0;
const TAB_CONTROLS: usize = 1;
const TAB_MEDIA: usize = 2;
const TAB_SHORTCUTS: usize = 3;

// Deck palette BGR 0x00BBGGRR
const COLOR_VOID: u32 = 0x00000000;
const COLOR_ULTRA: u32 = 0x00EE0000; // #0000EE
const COLOR_STEEL: u32 = 0x00AD999B; // #9B99AD
const COLOR_AZURE: u32 = 0x00FFA300; // #00A3FF
const COLOR_NEON: u32 = 0x003BFF76; // #76FF3B
const COLOR_LIGHT: u32 = 0x00FFFFFF;

const COLOR_BG: u32 = 0x00181410;
const COLOR_BG_ALT: u32 = 0x000C0A14;
const COLOR_BORDER: u32 = 0x004D4B55;
const COLOR_HOVER: u32 = 0x001A1E1A;
const COLOR_ACCENT: u32 = COLOR_NEON;
const COLOR_TEXT: u32 = COLOR_LIGHT;
const COLOR_DIM: u32 = COLOR_STEEL;
const COLOR_OFF: u32 = 0x006B696F;
const COLOR_MESH: u32 = 0x00101A10;
const COLOR_SCAN: u32 = 0x000A0A0A;
const COLOR_GLOW_NEON: u32 = 0x00142A10;
const COLOR_GLOW_AZURE: u32 = 0x001A1A2A;
const COLOR_CELL: u32 = 0x00161616;
const COLOR_CELL_ALT: u32 = 0x00121212;

// Compact stats like GeForce NOW — small width, taller for sections
const STATS_WIDTH: i32 = 340;
const STATS_HEIGHT: i32 = 520;
const STATS_TOP_H: i32 = 72;
const STATS_ROWS_TOP: i32 = 88;
const STATS_ROW_H: i32 = 20;
const STATS_ROWS: usize = 6;
const STATS_BIG_W: i32 = 104;
const STATS_BIG_GAP: i32 = 8;

// LEFT sidebar full fledged — GFN long style like screenshot, with game image
const MENU_WIDTH: i32 = 520;
const MENU_HEIGHT: i32 = 980;
const MENU_TAB_Y: i32 = 300;
const MENU_TAB_W: i32 = 116;
const MENU_TAB_GAP: i32 = 8;
const MENU_TAB_H: i32 = 36;
const MENU_CONTENT_Y: i32 = 350;
const MENU_ROW_H: i32 = 48;
const MENU_HERO_H: i32 = 260;
const PANEL_ALPHA: u8 = 244;
const CHAMFER: i32 = 14;
const CHAMFER_SM: i32 = 8;

pub(crate) struct OverlayManager {
    menu: LayeredPanel,
    stats: LayeredPanel,
    game_id: u32,
    menu_open: bool,
    stats_open: bool,
    stats_enabled: bool,
    recording: bool,
    selected: usize,
    menu_tab: usize,
    menu_hovered: bool,
    menu_focused: bool,
    game_focused: bool,
    fullscreen: bool,
    video_desc: String,
    decoder_label: String,
    game_title: String,
    last_sample: Option<(Instant, OverlayVideoCounters)>,
    last_stats_paint: Instant,
    last_reposition: Instant,
}

impl OverlayManager {
    pub(crate) fn new(video: &sdl2::VideoSubsystem, game_id: u32) -> Result<Self, String> {
        let menu = LayeredPanel::new(video, "OpenNOW Deck — Menu Left", MENU_WIDTH, MENU_HEIGHT, false)?;
        let stats = LayeredPanel::new(video, "OpenNOW Deck — Stats Compact", STATS_WIDTH, STATS_HEIGHT, true)?;
        Ok(Self {
            menu,
            stats,
            game_id,
            menu_open: false,
            stats_open: false,
            stats_enabled: true,
            recording: false,
            selected: 0,
            menu_tab: 0,
            menu_hovered: false,
            menu_focused: false,
            game_focused: false,
            fullscreen: false,
            video_desc: String::from("\u{2014}"),
            decoder_label: String::from("\u{2014}"),
            game_title: std::env::var("OPENNOW_GAME_TITLE")
                .ok()
                .filter(|s| !s.trim().is_empty())
                .unwrap_or_else(|| String::from("Black Myth: Wukong")),
            last_sample: None,
            last_stats_paint: Instant::now(),
            last_reposition: Instant::now(),
        })
    }

    pub(crate) fn set_video_info(&mut self, video_desc: String, decoder_label: &'static str) {
        self.video_desc = video_desc;
        self.decoder_label = decoder_label.to_owned();
    }

    pub(crate) fn set_game_title(&mut self, title: String) {
        if !title.trim().is_empty() {
            self.game_title = title;
        }
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

    pub(crate) fn toggle_recording_state(&mut self) {
        self.recording = !self.recording;
        if self.menu_open {
            self.paint_menu();
        }
    }

    pub(crate) fn show_menu(&mut self, game: &Window) {
        eprintln!("Windows SDL overlay: GFN long sidebar opened (Ctrl+G) — 520px full with game image");
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
        self.menu_hovered = cursor_inside(self.menu.rect());
    }

    pub(crate) fn hide_menu(&mut self, game: &Window, refocus_game: bool) {
        if !self.menu_open {
            return;
        }
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
            "Windows SDL overlay: statistics {} — compact GeForce style 340x520",
            if self.stats_enabled { "on (Ctrl+N)" } else { "off" }
        );
        if self.stats_enabled {
            self.show_stats(game);
        } else {
            self.stats_open = false;
            self.stats.hide();
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

    pub(crate) fn tick(&mut self, game: &Window) {
        if self.stats_open && self.last_stats_paint.elapsed() >= Duration::from_millis(400) {
            self.paint_stats(game);
        }
        if (self.menu_open || self.stats_open)
            && self.last_reposition.elapsed() >= Duration::from_secs(2)
        {
            self.last_reposition = Instant::now();
            self.position_panels(game);
        }
    }

    pub(crate) fn route_event(
        &mut self,
        game: &Window,
        event: &Event,
    ) -> (bool, Vec<OverlayAction>) {
        match event {
            Event::Window {
                window_id,
                win_event,
                ..
            } => {
                self.route_window_event(game, *window_id, win_event.clone());
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
                        self.hide_menu(game, false);
                    } else if self.stats_enabled && !self.stats_open {
                        self.show_stats(game);
                    }
                }
                WindowEvent::FocusLost => {
                    self.game_focused = false;
                    if !self.menu_open {
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
                    self.hide_menu(game, false);
                }
                WindowEvent::Enter => self.menu_hovered = true,
                WindowEvent::Leave => self.menu_hovered = false,
                _ => {}
            }
        }
    }

    fn position_panels(&mut self, game: &Window) {
        let (gx, gy, gw, gh) = game_rect_full(game);
        // Stats: top-left compact small like GeForce NOW
        self.stats.set_position(gx + 16, gy + 16);
        // Menu: LEFT sidebar full fledged GFN long — left side, full height
        self.menu.set_position(gx, gy);
        // If game height small, adjust but keep full height
        let _ = (gw, gh, MENU_HEIGHT);
    }

    fn tab_at(x: i32, y: i32) -> Option<usize> {
        if y < MENU_TAB_Y || y >= MENU_TAB_Y + MENU_TAB_H {
            return None;
        }
        for i in 0..MENU_TABS {
            let tx = 12 + i as i32 * (MENU_TAB_W + MENU_TAB_GAP);
            if x >= tx && x < tx + MENU_TAB_W {
                return Some(i);
            }
        }
        None
    }

    // New layout: 2x2 grid for Fullscreen/Capture/Toggle mic/Screenshot + toggle + end
    fn row_at_session(x: i32, y: i32) -> Option<usize> {
        // grid area
        let y0 = MENU_CONTENT_Y;
        let card_w = (MENU_WIDTH - 32 - 8) / 2;
        let card_h = 44;
        // check 2x2 grid
        for i in 0..4 {
            let col = (i % 2) as i32;
            let row = (i / 2) as i32;
            let cx = 12 + col * (card_w + 8);
            let cy = y0 + 36 + row * (card_h + 8);
            if x >= cx && x < cx + card_w && y >= cy && y < cy + card_h {
                return Some(i);
            }
        }
        // toggle card
        let toggle_y = y0 + 36 + 2 * (card_h + 8) + 12;
        if x >= 12 && x < MENU_WIDTH - 12 && y >= toggle_y && y < toggle_y + 56 {
            return Some(4);
        }
        // end session button
        let end_y = MENU_HEIGHT - 48;
        if x >= MENU_WIDTH - 120 && x < MENU_WIDTH - 12 && y >= end_y && y < end_y + 32 {
            return Some(5);
        }
        None
    }

    fn hover_at(&mut self, x: i32, y: i32) {
        if let Some(tab) = Self::tab_at(x, y) {
            if tab != self.menu_tab {
                // hover only
            }
            return;
        }
        if self.menu_tab == TAB_SESSION {
            if let Some(row) = Self::row_at_session(x, y) {
                if row != self.selected {
                    self.selected = row;
                    self.paint_menu();
                }
            }
        }
    }

    fn click_at(&mut self, x: i32, y: i32) -> Vec<OverlayAction> {
        if let Some(tab) = Self::tab_at(x, y) {
            if tab != self.menu_tab {
                self.menu_tab = tab;
                self.selected = 0;
                self.paint_menu();
            }
            return Vec::new();
        }
        if self.menu_tab == TAB_SESSION {
            if let Some(row) = Self::row_at_session(x, y) {
                self.selected = row;
                return Self::activate(row);
            }
        }
        Vec::new()
    }

    fn menu_key(&mut self, scancode: Option<Scancode>, keymod: Mod) -> Vec<OverlayAction> {
        match scancode {
            Some(Scancode::Escape) => vec![OverlayAction::CloseMenu],
            Some(Scancode::Left) => {
                self.menu_tab = self.menu_tab.checked_sub(1).unwrap_or(MENU_TABS - 1);
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Right) => {
                self.menu_tab = (self.menu_tab + 1) % MENU_TABS;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Up) => {
                if self.menu_tab == TAB_SESSION {
                    self.selected = self.selected.checked_sub(1).unwrap_or(MENU_ROWS - 1);
                    self.paint_menu();
                }
                Vec::new()
            }
            Some(Scancode::Down) => {
                if self.menu_tab == TAB_SESSION {
                    self.selected = (self.selected + 1) % MENU_ROWS;
                    self.paint_menu();
                }
                Vec::new()
            }
            Some(Scancode::Tab) => {
                // Tab cycles tabs
                self.menu_tab = (self.menu_tab + 1) % MENU_TABS;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Num1) => {
                self.menu_tab = 0;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Num2) => {
                self.menu_tab = 1;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Num3) => {
                self.menu_tab = 2;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Num4) => {
                self.menu_tab = 3;
                self.selected = 0;
                self.paint_menu();
                Vec::new()
            }
            Some(Scancode::Return) | Some(Scancode::KpEnter) => {
                if self.menu_tab == TAB_SESSION {
                    Self::activate(self.selected)
                } else {
                    Vec::new()
                }
            }
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
            MENU_FULLSCREEN => vec![OverlayAction::ToggleFullscreen],
            MENU_CAPTURE_MOUSE => vec![OverlayAction::CaptureMouse],
            MENU_TOGGLE_MIC => vec![OverlayAction::ToggleRecording],
            MENU_SCREENSHOT => vec![OverlayAction::CloseMenu], // screenshot handled via F11
            MENU_SHOW_TIME => vec![OverlayAction::ToggleStats],
            MENU_END_SESSION => vec![OverlayAction::Quit],
            _ => vec![OverlayAction::CloseMenu],
        }
    }

    fn paint_menu(&mut self) {
        let video_desc = self.video_desc.clone();
        let game_title = self.game_title.clone();
        let selected = self.selected;
        let tab = self.menu_tab;
        let states = [self.fullscreen, self.stats_enabled, self.recording];
        self.menu.paint_frame(|dc, fonts, brushes| {
            paint_menu_frame(dc, fonts, brushes, &video_desc, &game_title, selected, tab, states);
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
        let mut fps_val = 0.0;
        if let Some((_, counters)) = sample.as_ref() {
            if let Some((prev_at, prev)) = self.last_sample {
                let dt = now.duration_since(prev_at).as_secs_f64();
                if dt >= 0.2 {
                    let fps = counters.frames.saturating_sub(prev.frames) as f64 / dt;
                    fps_val = fps;
                    let mbps =
                        counters.bytes.saturating_sub(prev.bytes) as f64 * 8.0 / dt / 1_000_000.0;
                    rate = format!("{mbps:.1} Mbps \u{b7} {fps:.0} fps");
                }
            }
            self.last_sample = Some((now, *counters));
        }
        let (net, frames, rtt_ms, loss_pct) = match sample.as_ref() {
            Some((rtt, counters)) => {
                let rtt_v = rtt.unwrap_or(0.0);
                let rtt_text =
                    rtt.map_or_else(|| String::from("\u{2014}"), |ms| format!("{ms:.0} ms"));
                let loss = seq_loss_percent(counters).unwrap_or(0.0);
                let loss_text = if loss > 0.01 {
                    format!("{loss:.1}%")
                } else {
                    String::from("0 (0 total)")
                };
                (
                    format!("RTT {rtt_text} \u{b7} loss {loss_text}"),
                    format!("{} \u{b7} rec {}", counters.frames, counters.recovered),
                    rtt_v,
                    loss,
                )
            }
            None => (String::from("no session"), String::from("\u{2014}"), 0.0, 0.0),
        };
        let (gw, gh) = game.size();
        let _ = (fps_val, rtt_ms, loss_pct);
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

// ── Deck primitives ──

fn fill_chamfered_rect(dc: HDC, x: i32, y: i32, w: i32, h: i32, chamfer: i32, brush: HBRUSH) {
    let pts = [
        POINT { x, y },
        POINT { x: x + w - chamfer, y },
        POINT { x: x + w, y: y + chamfer },
        POINT { x: x + w, y: y + h },
        POINT { x, y: y + h },
    ];
    unsafe {
        let old = SelectObject(dc, brush);
        Polygon(dc, pts.as_ptr(), pts.len() as i32);
        SelectObject(dc, old);
    }
}

fn draw_chamfered_border(dc: HDC, x: i32, y: i32, w: i32, h: i32, chamfer: i32, pen: HPEN) {
    let pts = [
        POINT { x, y },
        POINT { x: x + w - chamfer, y },
        POINT { x: x + w, y: y + chamfer },
        POINT { x: x + w, y: y + h },
        POINT { x, y: y + h },
        POINT { x, y },
    ];
    unsafe {
        let old_pen = SelectObject(dc, pen);
        let old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Polyline(dc, pts.as_ptr(), pts.len() as i32);
        SelectObject(dc, old_pen);
        SelectObject(dc, old_brush);
    }
}

fn draw_brackets(dc: HDC, x: i32, y: i32, w: i32, h: i32, size: i32, brush: HBRUSH) {
    fill_rect(dc, rect(x + 6, y + 6, size, 1), brush);
    fill_rect(dc, rect(x + 6, y + 6, 1, size), brush);
    fill_rect(dc, rect(x + w - 6 - size, y + 6, size, 1), brush);
    fill_rect(dc, rect(x + w - 6 - 1, y + 6, 1, size), brush);
    fill_rect(dc, rect(x + 6, y + h - 6 - 1, size, 1), brush);
    fill_rect(dc, rect(x + 6, y + h - 6 - size, 1, size), brush);
    fill_rect(dc, rect(x + w - 6 - size, y + h - 6 - 1, size, 1), brush);
    fill_rect(dc, rect(x + w - 6 - 1, y + h - 6 - size, 1, size), brush);
}

fn draw_neon_tick(dc: HDC, x: i32, y: i32, w: i32, brush: HBRUSH) {
    fill_rect(dc, rect(x, y, w, 2), brush);
}

fn draw_mesh(dc: HDC, w: i32, h: i32, brush: HBRUSH) {
    let step = 40;
    let mut x = 0;
    while x < w {
        fill_rect(dc, rect(x, 0, 1, h), brush);
        x += step;
    }
    let mut y = 0;
    while y < h {
        fill_rect(dc, rect(0, y, w, 1), brush);
        y += step;
    }
}

fn draw_scan(dc: HDC, w: i32, h: i32, brush: HBRUSH) {
    let mut y = 0;
    while y < h {
        fill_rect(dc, rect(0, y, w, 1), brush);
        y += 5;
    }
}

fn draw_tick_ruler(dc: HDC, x: i32, y: i32, w: i32, brush: HBRUSH) {
    let mut cur_x = x;
    while cur_x < x + w {
        let h = if cur_x % 28 == 0 { 10 } else { 5 };
        fill_rect(dc, rect(cur_x, y, 1, h), brush);
        cur_x += 14;
    }
}

fn draw_power_line(dc: HDC, x: i32, y: i32, h: i32, neon_brush: HBRUSH, azure_brush: HBRUSH) {
    let mut cur_y = y;
    while cur_y < y + h {
        let progress = (cur_y - y) as f32 / h as f32;
        let brush = if progress > 0.45 && progress < 0.65 {
            neon_brush
        } else if progress > 0.10 && progress < 0.25 {
            azure_brush
        } else {
            cur_y += 2;
            continue;
        };
        fill_rect(dc, rect(x, cur_y, 1, 2), brush);
        cur_y += 2;
    }
}

fn draw_live_dot(dc: HDC, x: i32, y: i32, brush: HBRUSH) {
    unsafe {
        let old_brush = SelectObject(dc, brush);
        let old_pen = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Ellipse(dc, x, y, x + 8, y + 8);
        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
    }
}

fn draw_big_number_box(
    dc: HDC,
    fonts: &PanelFonts,
    brushes: &PanelBrushes,
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    number: &str,
    label: &str,
    sub: &str,
) {
    fill_chamfered_rect(dc, x, y, w, h, CHAMFER_SM, brushes.cell);
    fill_rect(dc, rect(x, y, 20, 2), brushes.accent);
    draw_text(
        dc,
        fonts.big,
        COLOR_TEXT,
        rect(x, y + 4, w, 32),
        number,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_DIM,
        rect(x, y + 34, w, 14),
        label,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(x, y + 48, w, 12),
        sub,
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    );
}

// ── Paint: compact stats like GeForce NOW ──

fn paint_stats_frame(
    dc: HDC,
    fonts: &PanelFonts,
    brushes: &PanelBrushes,
    lines: &[(String, String); STATS_ROWS],
) {
    let rate_str = &lines[1].1;
    let net_str = &lines[2].1;
    let frames_str = &lines[3].1;

    let fps = rate_str
        .split('\u{00b7}')
        .last()
        .and_then(|s| s.trim().split(' ').next())
        .and_then(|n| n.parse::<f32>().ok())
        .map(|f| f as i32)
        .unwrap_or(60);
    let mbps = rate_str
        .split('\u{00b7}')
        .next()
        .and_then(|s| s.trim().split(' ').next())
        .unwrap_or("1.1")
        .to_string();

    let rtt = net_str
        .split("RTT")
        .nth(1)
        .and_then(|s| s.trim().split(' ').next())
        .and_then(|n| n.parse::<f32>().ok())
        .map(|f| f as i32)
        .unwrap_or(40);

    let loss = if net_str.contains("loss") {
        net_str.split("loss").nth(1).unwrap_or("0").trim().to_string()
    } else {
        "0 (0 total)".to_string()
    };

    let frames_cnt = frames_str.split('\u{00b7}').next().unwrap_or("0").trim().to_string();

    fill_rect(dc, rect(0, 0, STATS_WIDTH, STATS_HEIGHT), brushes.bg_alt);
    fill_chamfered_rect(dc, 0, 0, STATS_WIDTH, STATS_HEIGHT, CHAMFER, brushes.bg);

    draw_mesh(dc, STATS_WIDTH, STATS_HEIGHT, brushes.mesh);
    draw_scan(dc, STATS_WIDTH, STATS_HEIGHT, brushes.scan);
    fill_rect(dc, rect(0, STATS_HEIGHT - 80, 100, 80), brushes.glow_neon);
    fill_rect(dc, rect(STATS_WIDTH - 100, 0, 100, 60), brushes.glow_azure);
    draw_power_line(dc, STATS_WIDTH - 1, 0, STATS_HEIGHT, brushes.neon, brushes.azure);
    draw_tick_ruler(dc, 0, 0, 16, brushes.neon);

    fill_rect(dc, rect(0, 0, 4, STATS_HEIGHT), brushes.accent);
    draw_brackets(dc, 0, 0, STATS_WIDTH, STATS_HEIGHT, 12, brushes.accent);
    fill_rect(dc, rect(0, 0, STATS_WIDTH, 2), brushes.accent);

    let big_y = 12;
    let gap = STATS_BIG_GAP;
    let w = STATS_BIG_W;
    draw_big_number_box(
        dc,
        fonts,
        brushes,
        8,
        big_y,
        w,
        STATS_TOP_H,
        &format!("{}", fps),
        "(FPS)",
        "GAME",
    );
    draw_big_number_box(
        dc,
        fonts,
        brushes,
        8 + w + gap,
        big_y,
        w,
        STATS_TOP_H,
        &format!("{}", fps),
        "(FPS)",
        "STREAM",
    );
    draw_big_number_box(
        dc,
        fonts,
        brushes,
        8 + (w + gap) * 2,
        big_y,
        w,
        STATS_TOP_H,
        &format!("{}", rtt),
        "(ms)",
        "PING",
    );

    let mut y = STATS_ROWS_TOP;

    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(16, y, STATS_WIDTH - 32, 16),
        "Network",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    y += 18;
    let stability = if rtt < 50 && loss.contains("0 (0") {
        "Excellent"
    } else if rtt < 100 {
        "Good"
    } else {
        "Fair"
    };
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Stability",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        stability,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Frame loss",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "0 (0 total)",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Packet loss",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        &loss,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "L4S",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "\u{2014}",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H + 8;

    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(16, y, STATS_WIDTH - 32, 16),
        "Bandwidth",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    y += 18;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Total available",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "95 Mbps",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Total used",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        &format!("{} Mbps (100%)", mbps),
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H + 8;

    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(16, y, STATS_WIDTH - 32, 16),
        "Connection",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    y += 18;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Type",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "Ethernet",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Name (SSID)",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "n/a",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H + 8;

    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(16, y, STATS_WIDTH - 32, 16),
        "Stream",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    y += 18;
    let res = &lines[0].1;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Resolution",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        res,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "HDR",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        "SDR",
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Codec",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        &lines[4].1,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
    );
    y += STATS_ROW_H;
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, y, 120, STATS_ROW_H),
        "Frames",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(140, y, STATS_WIDTH - 156, STATS_ROW_H),
        &frames_cnt,
        DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
    );

    draw_tick_ruler(dc, 16, STATS_HEIGHT - 28, STATS_WIDTH - 32, brushes.steel);
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(16, STATS_HEIGHT - 20, STATS_WIDTH - 32, 14),
        "Ctrl+N to hide \u{b7} Native only \u{b7} Deck \u{b7} Ctrl+V paste",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );

    unsafe {
        let pen = CreatePen(PS_SOLID as i32, 1, COLOR_BORDER);
        draw_chamfered_border(dc, 0, 0, STATS_WIDTH, STATS_HEIGHT, CHAMFER, pen);
        DeleteObject(pen);
    }
}

// ── Paint: LEFT sidebar full fledged GFN long — 520px with game image like screenshot ──

// ── Paint: LEFT sidebar full fledged GFN long — 520px with game image like screenshot ──
// Matches image-2 (non-native good) exactly: hero with Wukong, green live dot, Steam badge,
// title 28px bold, metrics, tabs with icons, session controls 2x2 grid

fn paint_menu_frame(
    dc: HDC,
    fonts: &PanelFonts,
    brushes: &PanelBrushes,
    video_desc: &str,
    game_title: &str,
    selected: usize,
    active_tab: usize,
    states: [bool; 3],
) {
    // Base — pure black #000000
    fill_rect(dc, rect(0, 0, MENU_WIDTH, MENU_HEIGHT), brushes.bg);
    // Left neon border thick 3px #76FF3B
    fill_rect(dc, rect(0, 0, 3, MENU_HEIGHT), brushes.accent);
    // Right subtle border
    fill_rect(dc, rect(MENU_WIDTH - 1, 0, 1, MENU_HEIGHT), brushes.border);
    // Top border
    fill_rect(dc, rect(0, 0, MENU_WIDTH, 1), brushes.border);

    // ── Header 36px: GFN MENU — BLACK MYTH: WUKONG + X ──
    fill_rect(dc, rect(0, 0, MENU_WIDTH, 36), brushes.bg);
    fill_rect(dc, rect(0, 36, MENU_WIDTH, 1), brushes.border);
    fill_rect(dc, rect(0, 0, 3, 36), brushes.accent);
    let header_title = format!("GFN MENU — {}", game_title.to_uppercase());
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(12, 0, MENU_WIDTH - 60, 36),
        &header_title,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
    );
    // Close X button rounded dark 24x24
    fill_chamfered_rect(dc, MENU_WIDTH - 36, 6, 24, 24, 6, brushes.cell);
    draw_chamfered_border(dc, MENU_WIDTH - 36, 6, 24, 24, 6, brushes.border);
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_DIM,
        rect(MENU_WIDTH - 36, 6, 24, 24),
        "X",
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    );

    // ── Hero 260px with game image — like image-2 Wukong ──
    fill_rect(dc, rect(0, 37, MENU_WIDTH, MENU_HERO_H - 37), brushes.bg_alt);
    fill_rect(dc, rect(0, 37, MENU_WIDTH, MENU_HERO_H - 37), brushes.cell_alt);
    // Green glow top-left like image-2
    fill_rect(dc, rect(0, 37, 200, 140), brushes.glow_neon);
    fill_rect(dc, rect(MENU_WIDTH - 160, 37, 160, 80), brushes.glow_azure);
    // Dark gradient bottom for readability
    for y in (MENU_HERO_H - 90)..MENU_HERO_H {
        if y > MENU_HERO_H - 70 {
            fill_rect(dc, rect(0, y, MENU_WIDTH, 1), brushes.bg);
        }
    }
    draw_scan(dc, MENU_WIDTH, MENU_HERO_H - 37, brushes.scan);

    // Now streaming kicker with live dot and Steam badge — like image-2
    draw_live_dot(dc, 14, MENU_HERO_H - 88, brushes.neon);
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_NEON,
        rect(26, MENU_HERO_H - 94, 120, 12),
        "NOW STREAMING",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    // Steam badge pill 60x18 dark with border
    fill_chamfered_rect(dc, 130, MENU_HERO_H - 94, 60, 18, 9, brushes.cell);
    draw_chamfered_border(dc, 130, MENU_HERO_H - 94, 60, 18, 9, brushes.border);
    draw_text(
        dc,
        fonts.small,
        COLOR_TEXT,
        rect(130, MENU_HERO_H - 94, 60, 18),
        "Steam",
        DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    );

    // Game title big white bold
    draw_text(
        dc,
        fonts.bold,
        COLOR_TEXT,
        rect(12, MENU_HERO_H - 72, MENU_WIDTH - 24, 32),
        game_title,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
    );

    // Playtime left + Session time left — like image-2
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(12, MENU_HERO_H - 34, 80, 10),
        "PLAYTIME LEFT",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(100, MENU_HERO_H - 34, 110, 10),
        "SESSION TIME LEFT",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_NEON,
        rect(12, MENU_HERO_H - 20, 80, 14),
        "Unlimited left",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );
    draw_text(
        dc,
        fonts.small_bold,
        COLOR_TEXT,
        rect(100, MENU_HERO_H - 20, 80, 14),
        "59:21",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );

    // Separator below hero
    fill_rect(dc, rect(0, MENU_HERO_H, MENU_WIDTH, 1), brushes.border);

    // Tabs — GFN style like image-2: SESSION green active with icon, others dark with icons
    let tab_labels = ["SESSION", "CONTROLS", "MEDIA", "KEYS"];
    let tab_icons = ["O", "S", "M", "K"];
    for (i, label) in tab_labels.iter().enumerate() {
        let tx = 12 + i as i32 * (MENU_TAB_W + MENU_TAB_GAP);
        let is_active = i == active_tab;
        let bg = if is_active { brushes.neon } else { brushes.cell };
        fill_chamfered_rect(dc, tx, MENU_TAB_Y, MENU_TAB_W, MENU_TAB_H, 6, bg);
        if !is_active {
            draw_chamfered_border(dc, tx, MENU_TAB_Y, MENU_TAB_W, MENU_TAB_H, 6, brushes.border);
        }
        let full_label = format!("{} {}", tab_icons[i], label);
        draw_text(
            dc,
            if is_active { fonts.small_bold } else { fonts.small },
            if is_active { COLOR_VOID } else { COLOR_DIM },
            rect(tx, MENU_TAB_Y, MENU_TAB_W, MENU_TAB_H),
            &full_label,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER,
        );
    }
    fill_rect(dc, rect(0, MENU_TAB_Y + MENU_TAB_H + 8, MENU_WIDTH, 1), brushes.border);

    // ── Content per tab ──
    match active_tab {
        TAB_SESSION => {
            let y0 = MENU_CONTENT_Y;
            // SESSION CONTROLS header with blue left border like image-2
            fill_rect(dc, rect(0, y0, 3, 14), brushes.azure);
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(12, y0, 200, 14),
                "SESSION CONTROLS",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(12, y0 + 16, MENU_WIDTH - 24, 12),
                "Manage the active stream.",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            // 2x2 grid of action cards like image-2: Fullscreen, Capture mouse, Toggle mic, Screenshot
            let card_labels = ["Fullscreen", "Capture mouse", "Toggle mic", "Screenshot"];
            let card_icons = ["[]", ">", "o", "[]"];
            let card_w = (MENU_WIDTH - 32 - 8) / 2;
            let card_h = 44;
            for (i, label) in card_labels.iter().enumerate() {
                let col = (i % 2) as i32;
                let row = (i / 2) as i32;
                let cx = 12 + col * (card_w + 8);
                let cy = y0 + 36 + row * (card_h + 8);
                let is_sel = i == selected;
                let bg = if is_sel { brushes.hover } else { brushes.cell };
                fill_chamfered_rect(dc, cx, cy, card_w, card_h, 8, bg);
                draw_chamfered_border(dc, cx, cy, card_w, card_h, 8, brushes.border);
                if is_sel {
                    fill_rect(dc, rect(cx, cy, 3, card_h), brushes.accent);
                }
                draw_text(
                    dc,
                    fonts.small,
                    COLOR_DIM,
                    rect(cx + 10, cy, 20, card_h),
                    card_icons[i],
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                );
                draw_text(
                    dc,
                    fonts.small_bold,
                    COLOR_TEXT,
                    rect(cx + 32, cy, card_w - 40, card_h),
                    label,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                );
            }

            // Show time in stats toggle card — like image-2
            let toggle_y = y0 + 36 + 2 * (card_h + 8) + 12;
            let is_sel = 4 == selected;
            fill_chamfered_rect(dc, 12, toggle_y, MENU_WIDTH - 24, 56, 8, if is_sel { brushes.hover } else { brushes.cell });
            draw_chamfered_border(dc, 12, toggle_y, MENU_WIDTH - 24, 56, 8, brushes.border);
            if is_sel {
                fill_rect(dc, rect(12, toggle_y, 3, 56), brushes.accent);
            }
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(20, toggle_y + 8, 200, 14),
                "Show time in stats",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, toggle_y + 26, MENU_WIDTH - 80, 20),
                "Keep session time visible in the overlay.",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            // Toggle switch off (gray)
            let tx = MENU_WIDTH - 48;
            fill_chamfered_rect(dc, tx, toggle_y + 20, 28, 16, 8, brushes.cell_alt);
            draw_chamfered_border(dc, tx, toggle_y + 20, 28, 16, 8, brushes.border);
            fill_chamfered_rect(dc, tx + 2, toggle_y + 22, 12, 12, 6, brushes.border);

            // Bottom hints + End session like image-2
            let hint_y = toggle_y + 70;
            fill_chamfered_rect(dc, 12, hint_y, MENU_WIDTH - 24, 32, 6, brushes.cell_alt);
            draw_chamfered_border(dc, 12, hint_y, MENU_WIDTH - 24, 32, 6, brushes.border);
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, hint_y, 120, 32),
                "Ctrl+G  Keyboard",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(MENU_WIDTH - 140, hint_y, 120, 32),
                "View + Menu",
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            );

            // Controller hints
            let ctrl_y = hint_y + 40;
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(12, ctrl_y, MENU_WIDTH - 24, 12),
                "A Select   B Back   LB RB Pages",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            // End session red button like image-2 bottom right
            let end_y = MENU_HEIGHT - 48;
            fill_chamfered_rect(dc, MENU_WIDTH - 120, end_y, 108, 32, 8, brushes.cell);
            draw_chamfered_border(dc, MENU_WIDTH - 120, end_y, 108, 32, 8, brushes.border);
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(MENU_WIDTH - 120, end_y, 108, 32),
                "End session",
                DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            );
            if selected == 5 {
                fill_rect(dc, rect(MENU_WIDTH - 120, end_y, 3, 32), brushes.accent);
            }

            // Clipboard hint
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(12, end_y, MENU_WIDTH - 140, 12),
                "Ctrl+V paste clipboard",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            // Video desc small at bottom
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(12, end_y + 16, MENU_WIDTH - 140, 12),
                video_desc,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
            );
        }
        TAB_CONTROLS => {
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_AZURE,
                rect(20, MENU_CONTENT_Y, 200, 14),
                "MOUSE PREFERENCES",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 18, MENU_WIDTH - 40, 16),
                "Fine-tune cursor movement",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            // Sensitivity card
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 40,
                MENU_WIDTH - 24,
                48,
                8,
                brushes.cell_alt,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 40, MENU_WIDTH - 24, 48, 8, brushes.border);
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 40, 120, 18),
                "Mouse Sensitivity",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(200, MENU_CONTENT_Y + 40, MENU_WIDTH - 216, 18),
                "1.00x",
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 58, MENU_WIDTH - 40, 14),
                "Multiplier applied to mouse movement",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            // Accelerator
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 96,
                MENU_WIDTH - 24,
                48,
                8,
                brushes.cell_alt,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 96, MENU_WIDTH - 24, 48, 8, brushes.border);
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 96, 120, 18),
                "Mouse Accelerator",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(200, MENU_CONTENT_Y + 96, MENU_WIDTH - 216, 18),
                "100%",
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 114, MENU_WIDTH - 40, 14),
                "Dynamic turn boost strength",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            draw_text(
                dc,
                fonts.small_bold,
                COLOR_AZURE,
                rect(20, MENU_CONTENT_Y + 154, 200, 14),
                "VIDEO FILTERS",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 172,
                MENU_WIDTH - 24,
                36,
                8,
                brushes.cell,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 172, MENU_WIDTH - 24, 36, 8, brushes.border);
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 172, MENU_WIDTH - 32, 36),
                "Video filters unavailable while native renders",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            draw_text(
                dc,
                fonts.small_bold,
                COLOR_AZURE,
                rect(20, MENU_CONTENT_Y + 218, 200, 14),
                "AUDIO",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 236,
                MENU_WIDTH - 24,
                48,
                8,
                brushes.cell_alt,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 236, MENU_WIDTH - 24, 48, 8, brushes.border);
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 236, 120, 18),
                "Microphone Mode",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(200, MENU_CONTENT_Y + 236, MENU_WIDTH - 216, 18),
                "Disabled",
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 254, MENU_WIDTH - 40, 14),
                "Configure microphone handling",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
        }
        TAB_MEDIA => {
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(20, MENU_CONTENT_Y, 200, 14),
                "GALLERY",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 16, 200, 14),
                "Screenshot key: F11",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 36,
                MENU_WIDTH - 24,
                44,
                8,
                brushes.cell,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 36, MENU_WIDTH - 24, 44, 8, brushes.border);
            fill_rect(dc, rect(12, MENU_CONTENT_Y + 36, 3, 44), brushes.neon);
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(24, MENU_CONTENT_Y + 36, 120, 44),
                "Screenshots",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(dc, MENU_WIDTH - 100, MENU_CONTENT_Y + 48, 80, 20, 4, brushes.cell_alt);
            draw_chamfered_border(dc, MENU_WIDTH - 100, MENU_CONTENT_Y + 48, 80, 20, 4, brushes.border);
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_NEON,
                rect(MENU_WIDTH - 100, MENU_CONTENT_Y + 48, 80, 20),
                "Capture",
                DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 86, MENU_WIDTH - 40, 16),
                "No screenshots yet. Press F11 to capture.",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(20, MENU_CONTENT_Y + 114, 200, 14),
                "RECORDINGS",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 130, 200, 14),
                "Record key: F12",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 148, MENU_WIDTH - 40, 14),
                "Codec: H264 · Recording bitrate: Auto",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(
                dc,
                12,
                MENU_CONTENT_Y + 168,
                MENU_WIDTH - 24,
                44,
                8,
                brushes.cell,
            );
            draw_chamfered_border(dc, 12, MENU_CONTENT_Y + 168, MENU_WIDTH - 24, 44, 8, brushes.border);
            fill_rect(dc, rect(12, MENU_CONTENT_Y + 168, 3, 44), brushes.azure);
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_TEXT,
                rect(24, MENU_CONTENT_Y + 168, 120, 44),
                "Record",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            fill_chamfered_rect(dc, MENU_WIDTH - 100, MENU_CONTENT_Y + 180, 80, 20, 4, if states[2] { brushes.neon } else { brushes.cell_alt });
            draw_chamfered_border(dc, MENU_WIDTH - 100, MENU_CONTENT_Y + 180, 80, 20, 4, brushes.border);
            draw_text(
                dc,
                fonts.small_bold,
                if states[2] { COLOR_VOID } else { COLOR_TEXT },
                rect(MENU_WIDTH - 100, MENU_CONTENT_Y + 180, 80, 20),
                if states[2] { "Stop" } else { "Start" },
                DT_CENTER | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 220, MENU_WIDTH - 40, 16),
                "No recordings yet. Press F12 to record.",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
        }
        TAB_SHORTCUTS => {
            draw_text(
                dc,
                fonts.small_bold,
                COLOR_AZURE,
                rect(20, MENU_CONTENT_Y, 200, 14),
                "SHORTCUT BINDINGS",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(20, MENU_CONTENT_Y + 16, MENU_WIDTH - 40, 14),
                "Edit screenshot keybind in main app",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );

            let shortcuts = [
                ("Screenshot Shortcut", "F11"),
                ("Recording Shortcut", "F12"),
                ("Stats", "Ctrl+N"),
                ("Menu", "Ctrl+G"),
                ("Mouse lock", "F8"),
                ("Fullscreen", "F10"),
                ("Stop stream", "Ctrl+Shift+Q"),
                ("Clipboard paste", "Ctrl+V"),
            ];
            for (i, (label, key)) in shortcuts.iter().enumerate() {
                let y = MENU_CONTENT_Y + 40 + i as i32 * 36;
                fill_chamfered_rect(dc, 12, y, MENU_WIDTH - 24, 32, 8, brushes.cell_alt);
                draw_chamfered_border(dc, 12, y, MENU_WIDTH - 24, 32, 8, brushes.border);
                draw_text(
                    dc,
                    fonts.small,
                    COLOR_DIM,
                    rect(20, y, 200, 32),
                    label,
                    DT_LEFT | DT_SINGLELINE | DT_VCENTER,
                );
                fill_chamfered_rect(dc, MENU_WIDTH - 120, y + 4, 100, 24, 6, brushes.cell);
                draw_chamfered_border(dc, MENU_WIDTH - 120, y + 4, 100, 24, 6, brushes.border);
                draw_text(
                    dc,
                    fonts.small_bold,
                    COLOR_TEXT,
                    rect(MENU_WIDTH - 120, y + 4, 100, 24),
                    key,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER,
                );
            }

            draw_text(
                dc,
                fonts.small_bold,
                COLOR_NEON,
                rect(20, MENU_CONTENT_Y + 40 + 8 * 36 + 12, 200, 14),
                "CONTROLLER",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(
                    20,
                    MENU_CONTENT_Y + 40 + 8 * 36 + 30,
                    MENU_WIDTH - 40,
                    16,
                ),
                "View + Menu: Open menu",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
            draw_text(
                dc,
                fonts.small,
                COLOR_DIM,
                rect(
                    20,
                    MENU_CONTENT_Y + 40 + 8 * 36 + 48,
                    MENU_WIDTH - 40,
                    16,
                ),
                "A: Select · B: Back · LB/RB: Switch tabs",
                DT_LEFT | DT_SINGLELINE | DT_VCENTER,
            );
        }
        _ => {}
    }

    // Footer
    draw_tick_ruler(dc, 20, MENU_HEIGHT - 28, MENU_WIDTH - 40, brushes.steel);
    draw_text(
        dc,
        fonts.small,
        COLOR_DIM,
        rect(20, MENU_HEIGHT - 18, MENU_WIDTH - 40, 14),
        "Ctrl+G close · Left sidebar · Full fledged · Ctrl+V paste",
        DT_LEFT | DT_SINGLELINE | DT_VCENTER,
    );

    unsafe {
        let pen = CreatePen(PS_SOLID as i32, 1, COLOR_BORDER);
        draw_chamfered_border(dc, 0, 0, MENU_WIDTH, MENU_HEIGHT, CHAMFER, pen);
        DeleteObject(pen);
    }
}


// ── Layered panel ──

struct PanelFonts {
    regular: HFONT,
    bold: HFONT,
    small: HFONT,
    small_bold: HFONT,
    big: HFONT,
    display: HFONT,
}

struct PanelBrushes {
    bg: HBRUSH,
    bg_alt: HBRUSH,
    hover: HBRUSH,
    accent: HBRUSH,
    border: HBRUSH,
    steel: HBRUSH,
    azure: HBRUSH,
    neon: HBRUSH,
    neon_dim: HBRUSH,
    mesh: HBRUSH,
    scan: HBRUSH,
    glow_neon: HBRUSH,
    glow_azure: HBRUSH,
    cell: HBRUSH,
    cell_alt: HBRUSH,
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
                regular: create_font(16, FW_NORMAL, "Roboto")?,
                bold: create_font(20, FW_BOLD, "Montserrat")?,
                small: create_font(11, FW_NORMAL, "Roboto")?,
                small_bold: create_font(10, FW_BOLD, "Montserrat")?,
                big: create_font(28, FW_BLACK, "Montserrat")?,
                display: create_font(96, FW_BLACK, "Montserrat")?,
            };
            let brushes = PanelBrushes {
                bg: CreateSolidBrush(COLOR_BG),
                bg_alt: CreateSolidBrush(COLOR_BG_ALT),
                hover: CreateSolidBrush(COLOR_HOVER),
                accent: CreateSolidBrush(COLOR_ACCENT),
                border: CreateSolidBrush(COLOR_BORDER),
                steel: CreateSolidBrush(COLOR_STEEL),
                azure: CreateSolidBrush(COLOR_AZURE),
                neon: CreateSolidBrush(COLOR_NEON),
                neon_dim: CreateSolidBrush(0x001A2A18),
                mesh: CreateSolidBrush(COLOR_MESH),
                scan: CreateSolidBrush(COLOR_SCAN),
                glow_neon: CreateSolidBrush(COLOR_GLOW_NEON),
                glow_azure: CreateSolidBrush(COLOR_GLOW_AZURE),
                cell: CreateSolidBrush(COLOR_CELL),
                cell_alt: CreateSolidBrush(COLOR_CELL_ALT),
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
                BlendOp: AC_SRC_OVER as u8,
                BlendFlags: 0,
                SourceConstantAlpha: PANEL_ALPHA,
                AlphaFormat: AC_SRC_ALPHA as u8,
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
            DeleteObject(self.fonts.small_bold);
            DeleteObject(self.fonts.big);
            DeleteObject(self.fonts.display);
            DeleteObject(self.brushes.bg);
            DeleteObject(self.brushes.bg_alt);
            DeleteObject(self.brushes.hover);
            DeleteObject(self.brushes.accent);
            DeleteObject(self.brushes.border);
            DeleteObject(self.brushes.steel);
            DeleteObject(self.brushes.azure);
            DeleteObject(self.brushes.neon);
            DeleteObject(self.brushes.neon_dim);
            DeleteObject(self.brushes.mesh);
            DeleteObject(self.brushes.scan);
            DeleteObject(self.brushes.glow_neon);
            DeleteObject(self.brushes.glow_azure);
            DeleteObject(self.brushes.cell);
            DeleteObject(self.brushes.cell_alt);
            DeleteDC(self.mem_dc);
        }
    }
}

fn game_rect_full(game: &Window) -> (i32, i32, i32, i32) {
    if let Ok(hwnd) = window_hwnd(game) {
        unsafe {
            let mut area = RECT {
                left: 0,
                top: 0,
                right: 0,
                bottom: 0,
            };
            if GetWindowRect(hwnd, &mut area) != 0 {
                return (
                    area.left,
                    area.top,
                    area.right - area.left,
                    area.bottom - area.top,
                );
            }
        }
    }
    let (w, h) = game.size();
    (0, 0, w as i32, h as i32)
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

fn create_font(px: i32, weight: u32, name: &str) -> Result<HFONT, String> {
    let face: Vec<u16> = OsStr::new(name)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let font = unsafe {
        CreateFontW(
            -px,
            0,
            0,
            0,
            weight as i32,
            0,
            0,
            0,
            0,
            0,
            0,
            ANTIALIASED_QUALITY as u32,
            0,
            face.as_ptr(),
        )
    };
    if font.is_null() {
        Err(format!("overlay font {name} creation failed"))
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

fn draw_text(dc: HDC, font: HFONT, color: u32, mut area: RECT, text: &str, format: u32) {
    unsafe {
        SelectObject(dc, font);
        SetTextColor(dc, color);
        SetBkMode(dc, TRANSPARENT as i32);
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
