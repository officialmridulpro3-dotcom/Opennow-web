use std::collections::HashSet;
use std::ffi::c_void;
use std::mem::{MaybeUninit, size_of};
use std::ptr::{null, null_mut};
use std::sync::atomic::{AtomicBool, AtomicIsize, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::thread::{self, JoinHandle};

use windows_sys::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows_sys::Win32::System::LibraryLoader::GetModuleHandleW;
use windows_sys::Win32::System::Threading::{
    GetCurrentThread, SetThreadPriority, THREAD_PRIORITY_ABOVE_NORMAL,
};
use windows_sys::Win32::UI::Input::{
    GetRawInputData, HRAWINPUT, MOUSE_MOVE_ABSOLUTE, RAWINPUT, RAWINPUTDEVICE, RAWINPUTHEADER,
    RID_INPUT, RIDEV_INPUTSINK, RIDEV_REMOVE, RIM_TYPEKEYBOARD, RIM_TYPEMOUSE,
    RegisterRawInputDevices,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    CREATESTRUCTW, CreateWindowExW, DefWindowProcW, DestroyWindow, DispatchMessageW, GWLP_USERDATA,
    GetForegroundWindow, GetMessageW, GetWindowLongPtrW, HWND_MESSAGE, MSG, PostMessageW,
    PostQuitMessage, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_DOWN,
    RI_MOUSE_BUTTON_5_UP, RI_MOUSE_HWHEEL, RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP,
    RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_DOWN,
    RI_MOUSE_RIGHT_BUTTON_UP, RI_MOUSE_WHEEL, RegisterClassW, SetWindowLongPtrW, TranslateMessage,
    WM_APP, WM_CLOSE, WM_INPUT, WM_KEYDOWN, WM_KEYUP, WM_NCCREATE, WM_NCDESTROY,
    WM_SYSKEYDOWN, WM_SYSKEYUP, WNDCLASSW,
};

use crate::media::{
    CapturedInput, CapturedInputQueue, StreamShortcutAction, StreamShortcutBindings,
};

const RAW_INPUT_CLASS: &[u16] = &[
    b'O' as u16,
    b'p' as u16,
    b'e' as u16,
    b'n' as u16,
    b'N' as u16,
    b'O' as u16,
    b'W' as u16,
    b'R' as u16,
    b'a' as u16,
    b'w' as u16,
    b'I' as u16,
    b'n' as u16,
    b'p' as u16,
    b'u' as u16,
    b't' as u16,
    0,
];
const WM_RAW_INPUT_REREGISTER: u32 = WM_APP + 1;

/// Virtual-key codes (Win32 `VK_*`) used for modifier tracking. Spelled out so
/// this module does not depend on the shape of the windows-sys constants.
const VK_RETURN: u16 = 0x0D;
const VK_SHIFT: u16 = 0x10;
const VK_CONTROL: u16 = 0x11;
const VK_MENU: u16 = 0x12;
const VK_LWIN: u16 = 0x5B;
const VK_RWIN: u16 = 0x5C;
const VK_LSHIFT: u16 = 0xA0;
const VK_RSHIFT: u16 = 0xA1;
const VK_LCONTROL: u16 = 0xA2;
const VK_RCONTROL: u16 = 0xA3;
const VK_LMENU: u16 = 0xA4;
const VK_RMENU: u16 = 0xA5;
/// 'G' — the fixed Ctrl+G stream guide chord, mirrored from
/// `output.rs::is_native_guide_shortcut`.
const VK_G: u16 = 0x47;

/// Modifier bits of the stream input protocol (`CapturedInput::Key`).
const MOD_SHIFT: u16 = 0x01;
const MOD_CTRL: u16 = 0x02;
const MOD_ALT: u16 = 0x04;
const MOD_GUI: u16 = 0x08;

/// Modifier flags for one key event, mirroring `sdl_modifiers` in `output.rs`:
/// the bit of the modifier key that is being pressed is left out of its own
/// event, so a Ctrl+G press reports Ctrl on the G and nothing on the Ctrl.
fn raw_modifiers(pressed: &HashSet<u16>, own: u16) -> u16 {
    let down = |keys: &[u16]| keys.iter().any(|key| pressed.contains(key));
    let mut flags = 0;
    if !matches!(own, VK_SHIFT | VK_LSHIFT | VK_RSHIFT) && down(&[VK_SHIFT, VK_LSHIFT, VK_RSHIFT])
    {
        flags |= MOD_SHIFT;
    }
    if !matches!(own, VK_CONTROL | VK_LCONTROL | VK_RCONTROL)
        && down(&[VK_CONTROL, VK_LCONTROL, VK_RCONTROL])
    {
        flags |= MOD_CTRL;
    }
    if !matches!(own, VK_MENU | VK_LMENU | VK_RMENU) && down(&[VK_MENU, VK_LMENU, VK_RMENU]) {
        flags |= MOD_ALT;
    }
    if !matches!(own, VK_LWIN | VK_RWIN) && down(&[VK_LWIN, VK_RWIN]) {
        flags |= MOD_GUI;
    }
    flags
}

struct RawInputState {
    foreground_owner: AtomicIsize,
    enabled: AtomicBool,
    relative_motion: AtomicBool,
    pressed_buttons: Mutex<HashSet<u8>>,
    /// The embedded shell owns window placement, so the video child never holds
    /// the keyboard focus and SDL cannot see the keys: this thread reads the
    /// keyboard itself and forwards the samples to the stream.
    forward_keyboard: AtomicBool,
    pressed_keys: Mutex<HashSet<u16>>,
    /// VKs of shortcut chords currently held down. Their key-up is swallowed so
    /// the remote game never receives half of a local shortcut.
    pressed_shortcuts: Mutex<HashSet<u16>>,
    /// Stream shortcut bindings configured by the shell (Ctrl+N / F8 / F11 /
    /// Ctrl+Shift+Q / ...). The Raw Input thread owns the keyboard whenever the
    /// embedded surface cannot hold focus, so it has to recognise the chords
    /// itself: otherwise every local shortcut would be typed into the game
    /// instead of reaching the deck.
    shortcuts: Mutex<StreamShortcutBindings>,
    /// Flagged when a left click lands in the game while the plane is embedded.
    /// The embedded child never takes the focus, so SDL never sees those clicks;
    /// the output thread turns the flag into mouse-look.
    lock_on_click: Option<Arc<AtomicBool>>,
    captured_input: Arc<CapturedInputQueue>,
}

pub(crate) struct WindowsRawInputController {
    state: Arc<RawInputState>,
    message_window: isize,
    worker: Option<JoinHandle<()>>,
}

impl WindowsRawInputController {
    pub(crate) fn start(
        foreground_owner: isize,
        captured_input: Arc<CapturedInputQueue>,
        forward_keyboard: bool,
        lock_on_click: Option<Arc<AtomicBool>>,
    ) -> Result<Self, String> {
        let state = Arc::new(RawInputState {
            foreground_owner: AtomicIsize::new(foreground_owner),
            enabled: AtomicBool::new(false),
            relative_motion: AtomicBool::new(false),
            pressed_buttons: Mutex::new(HashSet::new()),
            forward_keyboard: AtomicBool::new(forward_keyboard),
            pressed_keys: Mutex::new(HashSet::new()),
            pressed_shortcuts: Mutex::new(HashSet::new()),
            shortcuts: Mutex::new(StreamShortcutBindings::default()),
            lock_on_click,
            captured_input,
        });
        let thread_state = Arc::clone(&state);
        let (ready_sender, ready_receiver) = mpsc::sync_channel(1);
        let worker = thread::Builder::new()
            .name("opennow-raw-input".to_owned())
            .spawn(move || run_raw_input_thread(thread_state, ready_sender))
            .map_err(|error| format!("failed to spawn Raw Input thread: {error}"))?;
        let message_window = match ready_receiver.recv() {
            Ok(Ok(hwnd)) => hwnd,
            Ok(Err(error)) => {
                let _ = worker.join();
                return Err(error);
            }
            Err(_) => {
                let _ = worker.join();
                return Err("Raw Input thread exited during initialization".to_owned());
            }
        };
        Ok(Self {
            state,
            message_window,
            worker: Some(worker),
        })
    }

    pub(crate) fn set_foreground_owner(&self, foreground_owner: isize) {
        self.state
            .foreground_owner
            .store(foreground_owner, Ordering::Release);
    }

    /// Hand the thread the shell's shortcut bindings (see the `shortcuts`
    /// field). Safe to call again whenever the shell re-publishes settings.
    pub(crate) fn set_shortcut_bindings(&self, shortcuts: StreamShortcutBindings) {
        *self
            .state
            .shortcuts
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner) = shortcuts;
    }

    pub(crate) fn set_capture(&self, enabled: bool, relative_motion: bool) {
        let motion_changed = self
            .state
            .relative_motion
            .swap(relative_motion, Ordering::AcqRel)
            != relative_motion;
        let enabled_changed = self.state.enabled.swap(enabled, Ordering::AcqRel) != enabled;
        if enabled && (enabled_changed || motion_changed) {
            // SDL also uses Raw Input for relative mode. Register our dedicated
            // message window after SDL toggles the mode so motion is delivered
            // directly to this thread rather than SDL's event pump.
            unsafe {
                let _ = PostMessageW(self.message_window as HWND, WM_RAW_INPUT_REREGISTER, 0, 0);
            }
        } else if enabled_changed {
            release_pressed_buttons(&self.state);
            release_pressed_keys(&self.state);
        }
    }

    pub(crate) fn release_buttons(&self) {
        release_pressed_buttons(&self.state);
        release_pressed_keys(&self.state);
    }
}

impl Drop for WindowsRawInputController {
    fn drop(&mut self) {
        self.state.enabled.store(false, Ordering::Release);
        release_pressed_buttons(&self.state);
        release_pressed_keys(&self.state);
        unsafe {
            let _ = PostMessageW(self.message_window as HWND, WM_CLOSE, 0, 0);
        }
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

fn run_raw_input_thread(state: Arc<RawInputState>, ready: mpsc::SyncSender<Result<isize, String>>) {
    unsafe {
        let _ = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        let instance = GetModuleHandleW(null());
        let window_class = WNDCLASSW {
            lpfnWndProc: Some(raw_input_window_proc),
            hInstance: instance,
            lpszClassName: RAW_INPUT_CLASS.as_ptr(),
            ..Default::default()
        };
        let _ = RegisterClassW(&window_class);
        let state_pointer = Arc::as_ptr(&state);
        let hwnd = CreateWindowExW(
            0,
            RAW_INPUT_CLASS.as_ptr(),
            RAW_INPUT_CLASS.as_ptr(),
            0,
            0,
            0,
            0,
            0,
            HWND_MESSAGE,
            null_mut(),
            instance,
            state_pointer.cast::<c_void>(),
        );
        if hwnd.is_null() {
            let _ = ready.send(Err("failed to create Raw Input message window".to_owned()));
            return;
        }
        let keyboard = state.forward_keyboard.load(Ordering::Acquire);
        if !register_raw_input(hwnd, keyboard) {
            let _ = DestroyWindow(hwnd);
            let _ = ready.send(Err("failed to register Raw Input devices".to_owned()));
            return;
        }
        let _ = ready.send(Ok(hwnd as isize));
        let mut message = MSG::default();
        while GetMessageW(&mut message, null_mut(), 0, 0) > 0 {
            let _ = TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}

unsafe extern "system" fn raw_input_window_proc(
    hwnd: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    if message == WM_NCCREATE {
        let create = unsafe { &*(lparam as *const CREATESTRUCTW) };
        unsafe {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, create.lpCreateParams as isize);
        }
        return 1;
    }

    let state_pointer = unsafe { GetWindowLongPtrW(hwnd, GWLP_USERDATA) } as *const RawInputState;
    match message {
        WM_RAW_INPUT_REREGISTER => {
            // Internal wake after SDL changes relative mode: reclaim the raw
            // device registration for this dedicated message thread.
            let keyboard = if state_pointer.is_null() {
                false
            } else {
                unsafe { (*state_pointer).forward_keyboard.load(Ordering::Acquire) }
            };
            unsafe {
                let _ = register_raw_input(hwnd, keyboard);
            }
            0
        }
        WM_INPUT => {
            if !state_pointer.is_null() {
                unsafe {
                    process_raw_input(&*state_pointer, lparam as HRAWINPUT);
                }
            }
            0
        }
        WM_CLOSE => {
            unsafe {
                unregister_raw_input();
                let _ = DestroyWindow(hwnd);
            }
            0
        }
        WM_NCDESTROY => {
            unsafe {
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                PostQuitMessage(0);
            }
            0
        }
        _ => unsafe { DefWindowProcW(hwnd, message, wparam, lparam) },
    }
}

/// Register the mouse, and — when the shell owns window placement — the
/// keyboard too. `RIDEV_INPUTSINK` delivers input to this message-only window
/// even while another window (the shell's WebView) holds the focus; samples
/// typed into other applications are filtered out by the foreground check in
/// `process_raw_input`.
unsafe fn register_raw_input(hwnd: HWND, keyboard: bool) -> bool {
    let devices = [
        RAWINPUTDEVICE {
            usUsagePage: 0x01,
            usUsage: 0x02,
            dwFlags: RIDEV_INPUTSINK,
            hwndTarget: hwnd,
        },
        RAWINPUTDEVICE {
            usUsagePage: 0x01,
            usUsage: 0x06,
            dwFlags: RIDEV_INPUTSINK,
            hwndTarget: hwnd,
        },
    ];
    let count = if keyboard { 2 } else { 1 };
    unsafe {
        RegisterRawInputDevices(devices.as_ptr(), count, size_of::<RAWINPUTDEVICE>() as u32) != 0
    }
}

unsafe fn unregister_raw_input() {
    for usage in [0x02u16, 0x06] {
        let device = RAWINPUTDEVICE {
            usUsagePage: 0x01,
            usUsage: usage,
            dwFlags: RIDEV_REMOVE,
            hwndTarget: null_mut(),
        };
        unsafe {
            let _ = RegisterRawInputDevices(&device, 1, size_of::<RAWINPUTDEVICE>() as u32);
        }
    }
}

unsafe fn process_raw_input(state: &RawInputState, handle: HRAWINPUT) {
    if !state.enabled.load(Ordering::Acquire) {
        return;
    }
    let mut raw = MaybeUninit::<RAWINPUT>::uninit();
    let mut size = size_of::<RAWINPUT>() as u32;
    let copied = unsafe {
        GetRawInputData(
            handle,
            RID_INPUT,
            raw.as_mut_ptr().cast::<c_void>(),
            &mut size,
            size_of::<RAWINPUTHEADER>() as u32,
        )
    };
    if copied == u32::MAX || copied < size_of::<RAWINPUTHEADER>() as u32 {
        return;
    }
    let raw = unsafe { raw.assume_init() };
    if raw.header.dwType == RIM_TYPEKEYBOARD {
        process_raw_keyboard(state, &raw);
        return;
    }
    if raw.header.dwType != RIM_TYPEMOUSE {
        return;
    }
    let mouse = unsafe { raw.data.mouse };
    let owns_foreground =
        unsafe { GetForegroundWindow() } as isize == state.foreground_owner.load(Ordering::Acquire);
    if owns_foreground
        && state.relative_motion.load(Ordering::Acquire)
        && mouse.usFlags & MOUSE_MOVE_ABSOLUTE == 0
    {
        // SDL continues to own absolute cursor coordinates. This thread owns raw relative
        // deltas, plus buttons and wheel in both cursor modes.
        push_mouse_delta(&state.captured_input, mouse.lLastX, mouse.lLastY);
    }

    let buttons = unsafe { mouse.Anonymous.Anonymous };
    if owns_foreground
        && u32::from(buttons.usButtonFlags) & RI_MOUSE_LEFT_BUTTON_DOWN != 0
        && let Some(flag) = state.lock_on_click.as_ref()
    {
        // The plane is embedded and never focused, so this thread — not SDL —
        // is what sees the click. Ask the output thread for mouse-look.
        flag.store(true, Ordering::Release);
    }
    let button_flags = if owns_foreground {
        buttons.usButtonFlags
    } else {
        // A click can transiently move foreground before WM_INPUT delivers the
        // matching release. Never discard releases for buttons that we already
        // sent down: doing so leaves both the local de-duplicator and host stuck.
        buttons.usButtonFlags & raw_mouse_button_up_mask()
    };
    push_raw_mouse_buttons(state, button_flags);
    if owns_foreground && u32::from(buttons.usButtonFlags) & RI_MOUSE_WHEEL != 0 {
        state.captured_input.push(CapturedInput::MouseWheel {
            delta_x: 0,
            delta_y: buttons.usButtonData as i16,
        });
    }
    if owns_foreground && u32::from(buttons.usButtonFlags) & RI_MOUSE_HWHEEL != 0 {
        state.captured_input.push(CapturedInput::MouseWheel {
            delta_x: buttons.usButtonData as i16,
            delta_y: 0,
        });
    }
}

/// Forward one keyboard sample. Windows reports held keys as a stream of make
/// messages, so repeats (a key that is already down) are dropped — exactly like
/// the SDL path does, since the remote side generates its own repeat.
unsafe fn process_raw_keyboard(state: &RawInputState, raw: &RAWINPUT) {
    if !state.forward_keyboard.load(Ordering::Acquire) {
        return;
    }
    if unsafe { GetForegroundWindow() } as isize != state.foreground_owner.load(Ordering::Acquire) {
        // Keys typed into another application are none of our business.
        return;
    }
    let keyboard = unsafe { raw.data.keyboard };
    let key = keyboard.VKey;
    // 0 is unset and 0xFF is a fake key produced by some KVM/remote tools.
    if key == 0 || key == u16::from(u8::MAX) {
        return;
    }
    let pressed = keyboard.Message == WM_KEYDOWN || keyboard.Message == WM_SYSKEYDOWN;
    let modifiers = {
        let mut pressed_keys = state
            .pressed_keys
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if !state.enabled.load(Ordering::Acquire) {
            return;
        }
        if pressed {
            if !pressed_keys.insert(key) {
                return;
            }
        } else if !pressed_keys.remove(&key) {
            return;
        }
        raw_modifiers(&pressed_keys, key)
    };
    // Local stream chords are recognised here, exactly like the SDL event path
    // does when the window holds the focus: Ctrl+G opens the deck, Alt+Enter or
    // the configured F11 toggle fullscreen, Ctrl+N the stats strip, and so on.
    // Everything else is gameplay input.
    if pressed {
        if is_guide_key(key, modifiers) {
            state
                .pressed_shortcuts
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .insert(key);
            state.captured_input.push(CapturedInput::Guide);
            eprintln!("Raw Input shortcut: Ctrl+G (guide/deck)");
            return;
        }
        if is_alt_enter(key, modifiers) {
            state
                .pressed_shortcuts
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .insert(key);
            state
                .captured_input
                .push(CapturedInput::Shortcut(StreamShortcutAction::ToggleFullscreen));
            eprintln!("Raw Input shortcut: Alt+Enter (toggle fullscreen)");
            return;
        }
        let action = {
            let shortcuts = state
                .shortcuts
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            shortcuts.action(key, modifiers)
        };
        if let Some(action) = action {
            state
                .pressed_shortcuts
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner)
                .insert(key);
            state.captured_input.push(CapturedInput::Shortcut(action));
            // One line per chord press: with the game focused this is the only
            // record that a shortcut was seen, and it is what makes a
            // "shortcut does nothing" report diagnosable from server.log.
            eprintln!(
                "Raw Input shortcut: {} (virtual key {key:#04x}, modifiers {modifiers:#04x})",
                action.protocol_name()
            );
            return;
        }
    } else if state
        .pressed_shortcuts
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .remove(&key)
    {
        return;
    }
    state.captured_input.push(CapturedInput::Key {
        virtual_key: key,
        modifiers,
        pressed,
    });
}

fn release_pressed_keys(state: &RawInputState) {
    state
        .pressed_shortcuts
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner)
        .clear();
    let mut pressed_keys = state
        .pressed_keys
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    for key in pressed_keys.drain() {
        state.captured_input.push(CapturedInput::Key {
            virtual_key: key,
            modifiers: 0,
            pressed: false,
        });
    }
}

/// Ctrl+G is the fixed stream guide chord (see `output.rs`). `modifiers`
/// already excludes the pressed key's own bit, so an exact match keeps Alt or
/// Shift + Ctrl + G available to the game.
fn is_guide_key(key: u16, modifiers: u16) -> bool {
    key == VK_G && modifiers == MOD_CTRL
}

/// Alt+Enter is the universal game-fullscreen chord and must stay local even
/// where the configured binding only lists F11 (Fn-lock laptops).
fn is_alt_enter(key: u16, modifiers: u16) -> bool {
    key == VK_RETURN && modifiers & MOD_ALT != 0
}

fn raw_mouse_button_up_mask() -> u16 {
    (RI_MOUSE_LEFT_BUTTON_UP
        | RI_MOUSE_MIDDLE_BUTTON_UP
        | RI_MOUSE_RIGHT_BUTTON_UP
        | RI_MOUSE_BUTTON_4_UP
        | RI_MOUSE_BUTTON_5_UP) as u16
}

fn push_raw_mouse_buttons(state: &RawInputState, flags: u16) {
    const BUTTON_FLAGS: &[(u32, u32, u8)] = &[
        (RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, 1),
        (RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 2),
        (RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, 3),
        (RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, 4),
        (RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, 5),
    ];
    let flags = u32::from(flags);
    for &(down, up, button) in BUTTON_FLAGS {
        if flags & down != 0 {
            push_mouse_button(state, button, true);
        }
        if flags & up != 0 {
            push_mouse_button(state, button, false);
        }
    }
}

fn push_mouse_button(state: &RawInputState, button: u8, pressed: bool) {
    let mut pressed_buttons = state
        .pressed_buttons
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    if !state.enabled.load(Ordering::Acquire) {
        return;
    }
    let changed = if pressed {
        pressed_buttons.insert(button)
    } else {
        pressed_buttons.remove(&button)
    };
    if changed {
        state
            .captured_input
            .push(CapturedInput::MouseButton { button, pressed });
    }
}

fn release_pressed_buttons(state: &RawInputState) {
    let mut pressed_buttons = state
        .pressed_buttons
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    for button in pressed_buttons.drain() {
        state.captured_input.push(CapturedInput::MouseButton {
            button,
            pressed: false,
        });
    }
}

fn push_mouse_delta(queue: &CapturedInputQueue, mut delta_x: i32, mut delta_y: i32) {
    while delta_x != 0 || delta_y != 0 {
        let x = delta_x.clamp(i32::from(i16::MIN), i32::from(i16::MAX)) as i16;
        let y = delta_y.clamp(i32::from(i16::MIN), i32::from(i16::MAX)) as i16;
        queue.push(CapturedInput::MouseMove {
            delta_x: x,
            delta_y: y,
        });
        delta_x -= i32::from(x);
        delta_y -= i32::from(y);
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;
    use std::sync::atomic::Ordering;

    use super::{
        MOD_ALT, MOD_CTRL, MOD_SHIFT, RawInputState, VK_LCONTROL, VK_LSHIFT, VK_MENU,
        push_mouse_delta, push_raw_mouse_buttons, raw_modifiers, release_pressed_buttons,
    };
    use crate::media::{
        CapturedInput, CapturedInputQueue, StreamShortcutAction, StreamShortcutBindings,
    };
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_DOWN,
    };

    fn state() -> RawInputState {
        RawInputState {
            foreground_owner: Default::default(),
            enabled: true.into(),
            relative_motion: false.into(),
            pressed_buttons: Default::default(),
            forward_keyboard: true.into(),
            pressed_keys: Default::default(),
            pressed_shortcuts: Default::default(),
            shortcuts: Default::default(),
            lock_on_click: None,
            captured_input: Arc::new(CapturedInputQueue::default()),
        }
    }

    #[test]
    fn raw_chords_never_reach_the_game() {
        let state = state();
        {
            let mut pressed = state
                .pressed_keys
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            pressed.insert(VK_LCONTROL);
            pressed.insert(VK_G);
        }
        // Ctrl+G is the fixed guide chord ...
        assert!(super::is_guide_key(VK_G, MOD_CTRL));
        // ... while Alt+Enter and the configured bindings stay local too.
        assert!(super::is_alt_enter(VK_RETURN, MOD_ALT));
        assert!(!super::is_guide_key(VK_G, MOD_CTRL | MOD_SHIFT));
        // Ctrl+Shift+Q (VK_Q = 0x51) is the default stop-stream binding.
        assert_eq!(
            StreamShortcutBindings::default().action(0x51, MOD_CTRL | MOD_SHIFT),
            Some(StreamShortcutAction::StopStream)
        );
        drop(state);
    }

    #[test]
    fn modifiers_exclude_the_pressed_modifier_key() {
        let mut pressed = std::collections::HashSet::new();
        pressed.insert(VK_LCONTROL);
        pressed.insert(u16::from(b'G'));
        // Ctrl press: its own bit is not reported on the modifier event.
        assert_eq!(raw_modifiers(&pressed, VK_LCONTROL), 0);
        // G press while Ctrl is held: Ctrl is reported, matching sdl_modifiers.
        assert_eq!(raw_modifiers(&pressed, u16::from(b'G')), MOD_CTRL);
        pressed.insert(VK_LSHIFT);
        assert_eq!(raw_modifiers(&pressed, u16::from(b'G')), MOD_CTRL | MOD_SHIFT);
        pressed.clear();
        pressed.insert(VK_MENU);
        assert_eq!(raw_modifiers(&pressed, VK_MENU), 0);
        assert_eq!(raw_modifiers(&pressed, u16::from(b'A')), MOD_ALT);
    }

    #[test]
    fn large_raw_mouse_deltas_are_split_without_loss() {
        let queue = CapturedInputQueue::default();
        push_mouse_delta(&queue, 40_000, -40_000);

        let mut total_x = 0_i32;
        let mut total_y = 0_i32;
        while let Some(CapturedInput::MouseMove { delta_x, delta_y }) = queue.take() {
            total_x += i32::from(delta_x);
            total_y += i32::from(delta_y);
        }
        assert_eq!((total_x, total_y), (40_000, -40_000));
    }

    #[test]
    fn raw_buttons_keep_one_owner_across_cursor_mode_changes() {
        let state = state();
        push_raw_mouse_buttons(
            &state,
            (RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_DOWN) as u16,
        );
        // A server cursor update can switch motion between SDL absolute and Raw Input relative,
        // but button ownership and pressed state remain on this controller.
        state.relative_motion.store(true, Ordering::Release);
        push_raw_mouse_buttons(&state, RI_MOUSE_LEFT_BUTTON_DOWN as u16);
        state.relative_motion.store(false, Ordering::Release);
        push_raw_mouse_buttons(&state, RI_MOUSE_LEFT_BUTTON_UP as u16);

        assert_eq!(
            state.captured_input.take(),
            Some(CapturedInput::MouseButton {
                button: 1,
                pressed: true,
            })
        );
        assert_eq!(
            state.captured_input.take(),
            Some(CapturedInput::MouseButton {
                button: 3,
                pressed: true,
            })
        );
        assert_eq!(
            state.captured_input.take(),
            Some(CapturedInput::MouseButton {
                button: 1,
                pressed: false,
            })
        );

        state.enabled.store(false, Ordering::Release);
        release_pressed_buttons(&state);
        assert_eq!(
            state.captured_input.take(),
            Some(CapturedInput::MouseButton {
                button: 3,
                pressed: false,
            })
        );
        assert_eq!(state.captured_input.take(), None);
    }
}
