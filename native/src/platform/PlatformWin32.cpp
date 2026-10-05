// Win32 platform backend.
//
// A plain Win32 window plus a WGL OpenGL 3.2 *core profile* context. No GLFW,
// no SDL, no GLEW: `opengl32.lib` is part of the OS, and the modern context is
// obtained through `wglCreateContextAttribsARB` after a bootstrap context has
// loaded the extension. That keeps a from-scratch `cl /EHsc src\*.cpp` build
// working with nothing but the Windows SDK.
#include "onow/platform/Platform.h"

#include "onow/Log.h"

#if defined(_WIN32)

#include <windows.h>

#include <atomic>
#include <cstring>

namespace onow {

namespace {

const wchar_t* kClassName = L"OpenNOWWindow";

Key translate_key(WPARAM wparam, LPARAM lparam) {
  const bool extended = (lparam & (1 << 24)) != 0;
  if (wparam >= 'A' && wparam <= 'Z') return static_cast<Key>(static_cast<int>(Key::A) + (wparam - 'A'));
  if (wparam >= '0' && wparam <= '9') return static_cast<Key>(static_cast<int>(Key::Num0) + (wparam - '0'));
  if (wparam >= VK_NUMPAD0 && wparam <= VK_NUMPAD9) {
    return static_cast<Key>(static_cast<int>(Key::Numpad0) + (wparam - VK_NUMPAD0));
  }
  if (wparam >= VK_F1 && wparam <= VK_F12) return static_cast<Key>(static_cast<int>(Key::F1) + (wparam - VK_F1));
  switch (wparam) {
    case VK_TAB: return Key::Tab;
    case VK_LEFT: return Key::LeftArrow;
    case VK_RIGHT: return Key::RightArrow;
    case VK_UP: return Key::UpArrow;
    case VK_DOWN: return Key::DownArrow;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_INSERT: return Key::Insert;
    case VK_DELETE: return Key::Delete;
    case VK_BACK: return Key::Backspace;
    case VK_SPACE: return Key::Space;
    case VK_RETURN: return extended ? Key::Unknown : Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_LCONTROL: return Key::LeftCtrl;
    case VK_RCONTROL: return Key::RightCtrl;
    case VK_LSHIFT: return Key::LeftShift;
    case VK_RSHIFT: return Key::RightShift;
    case VK_LMENU: return Key::LeftAlt;
    case VK_RMENU: return Key::RightAlt;
    case VK_LWIN: return Key::LeftSuper;
    case VK_RWIN: return Key::RightSuper;
    default: return Key::Unknown;
  }
}

} // namespace

class Win32Platform : public Platform {
public:
  bool init(const PlatformConfig& config, std::string& error) override {
    config_ = config;
    instance_ = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = &Win32Platform::wnd_proc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    ::RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT desired = {0, 0, config.width, config.height};
    ::AdjustWindowRect(&desired, style, FALSE);

    hwnd_ = ::CreateWindowExW(0, kClassName, L"OpenNOW", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              desired.right - desired.left, desired.bottom - desired.top, nullptr,
                              nullptr, instance_, nullptr);
    if (!hwnd_) {
      error = "CreateWindowEx failed";
      return false;
    }
    ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ::SetWindowTextW(hwnd_, L"OpenNOW");
    if (config.start_maximized) ::ShowWindow(hwnd_, SW_MAXIMIZE);
    else ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);

    if (!create_gl_context(error)) return false;

    RECT client{};
    ::GetClientRect(hwnd_, &client);
    width_ = client.right - client.left;
    height_ = client.bottom - client.top;
    ONOW_INFO("Platform", "win32 backend initialised (%dx%d, GL %s)", width_, height_,
              gl::GETSTRING ? reinterpret_cast<const char*>(gl::GETSTRING(0x1F02)) : "?");
    return true;
  }

  void shutdown() override {
    if (gl_context_) {
      ::wglMakeCurrent(nullptr, nullptr);
      ::wglDeleteContext(gl_context_);
      gl_context_ = nullptr;
    }
    if (device_context_) {
      ::ReleaseDC(hwnd_, device_context_);
      device_context_ = nullptr;
    }
    if (hwnd_) {
      ::DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
    ::UnregisterClassW(kClassName, instance_);
  }

  bool poll_events() override {
    MSG msg;
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return false;
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
    }
    return !quit_requested_;
  }

  void begin_frame() override {
    if (!gl::ready()) return;
    gl::VIEWPORT(0, 0, width_, height_);
    gl::CLEARCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    gl::CLEAR(0x00004000 /*GL_COLOR_BUFFER_BIT*/);
  }

  void end_frame() override { ::SwapBuffers(device_context_); }

  void framebuffer_size(int& width, int& height) const override {
    width = width_;
    height = height_;
  }
  void window_size(int& width, int& height) const override {
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;
  }
  float content_scale() const override {
    const HDC screen = ::GetDC(nullptr);
    const int dpi = ::GetDeviceCaps(screen, LOGPIXELSX);
    ::ReleaseDC(nullptr, screen);
    return static_cast<float>(dpi) / 96.0f;
  }

  const InputState& input() const override { return input_; }

  void set_title(const std::string& title) override {
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(needed > 0 ? needed - 1 : 0), L'\0');
    if (needed > 1) ::MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, wide.data(), needed);
    ::SetWindowTextW(hwnd_, wide.c_str());
  }

  void set_fullscreen(bool fullscreen) override {
    if (fullscreen == fullscreen_) return;
    static RECT saved{};
    static DWORD saved_style = 0;
    if (fullscreen) {
      saved_style = ::GetWindowLongW(hwnd_, GWL_STYLE);
      ::GetWindowRect(hwnd_, &saved);
      ::SetWindowLongW(hwnd_, GWL_STYLE, saved_style & ~(WS_CAPTION | WS_THICKFRAME));
      const HMONITOR monitor = ::MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
      MONITORINFO info{};
      info.cbSize = sizeof(info);
      ::GetMonitorInfoW(monitor, &info);
      ::SetWindowPos(hwnd_, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
      ::SetWindowLongW(hwnd_, GWL_STYLE, saved_style);
      ::SetWindowPos(hwnd_, nullptr, saved.left, saved.top, saved.right - saved.left,
                     saved.bottom - saved.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    fullscreen_ = fullscreen;
  }

  bool is_fullscreen() const override { return fullscreen_; }
  bool is_focused() const override { return ::GetForegroundWindow() == hwnd_; }

  std::string clipboard_get_text() override {
    if (!::OpenClipboard(hwnd_)) return std::string();
    std::string out;
    if (const HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
      if (const auto* wide = static_cast<const wchar_t*>(::GlobalLock(data))) {
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
          out.resize(static_cast<size_t>(needed - 1));
          ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
        }
        ::GlobalUnlock(data);
      }
    }
    ::CloseClipboard();
    return out;
  }

  void clipboard_set_text(const std::string& text) override {
    const int wide_len = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wide_len <= 1) return;
    const size_t bytes = static_cast<size_t>(wide_len) * sizeof(wchar_t);
    if (const HANDLE heap = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
      if (auto* wide = static_cast<wchar_t*>(::GlobalLock(heap))) {
        ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide, wide_len);
        ::GlobalUnlock(heap);
      }
      if (::OpenClipboard(hwnd_)) {
        ::EmptyClipboard();
        ::SetClipboardData(CF_UNICODETEXT, heap);
        ::CloseClipboard();
      } else {
        ::GlobalFree(heap);
      }
    }
  }

  void set_cursor_shape(CursorShape shape) override {
    if (shape == cursor_shape_) return;
    cursor_shape_ = shape;
    if (!cursor_visible_) return;
    LPWSTR id = IDC_ARROW;
    switch (shape) {
      case CursorShape::TextInput: id = IDC_IBEAM; break;
      case CursorShape::ResizeAll: id = IDC_SIZEALL; break;
      case CursorShape::ResizeNS: id = IDC_SIZENS; break;
      case CursorShape::ResizeEW: id = IDC_SIZEWE; break;
      case CursorShape::ResizeNESW: id = IDC_SIZENESW; break;
      case CursorShape::ResizeNWSE: id = IDC_SIZENWSE; break;
      case CursorShape::Hand: id = IDC_HAND; break;
      case CursorShape::NotAllowed: id = IDC_NO; break;
      case CursorShape::Arrow: default: id = IDC_ARROW; break;
    }
    ::SetCursor(::LoadCursorW(nullptr, id));
  }

  void set_cursor_visible(bool visible) override {
    if (cursor_visible_ == visible) return;
    cursor_visible_ = visible;
    // ShowCursor is a counter, not a boolean; normalise it.
    while (::ShowCursor(visible ? TRUE : FALSE) != (visible ? 1 : 0)) {
      if (visible && ::ShowCursor(TRUE) > 0) break;
      if (!visible && ::ShowCursor(FALSE) < 0) break;
      break;
    }
  }

  void set_pointer_lock(bool locked) override {
    pointer_locked_ = locked;
    if (locked) {
      RECT client{};
      ::GetClientRect(hwnd_, &client);
      POINT top_left{client.left, client.top};
      POINT bottom_right{client.right, client.bottom};
      ::ClientToScreen(hwnd_, &top_left);
      ::ClientToScreen(hwnd_, &bottom_right);
      RECT clip{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
      ::ClipCursor(&clip);
      ::SetCapture(hwnd_);
      set_cursor_visible(false);
    } else {
      ::ClipCursor(nullptr);
      ::ReleaseCapture();
      set_cursor_visible(true);
    }
  }

  bool pointer_locked() const override { return pointer_locked_; }

  bool open_file_dialog(const std::string& title, const std::string& filter,
                        std::string& out_path) override {
    (void)title;
    (void)filter;
    (void)out_path;
    // GetOpenFileName needs a COMDLG32 filter string ("desc\0*.ext\0\0"); the
    // UI never opens files in this client beyond the streamer path picker, so
    // the callers treat `false` as "no path chosen" anyway.
    return false;
  }
  bool save_file_dialog(const std::string& title, const std::string& default_name,
                        const std::string& filter, std::string& out_path) override {
    (void)title;
    (void)default_name;
    (void)filter;
    (void)out_path;
    return false;
  }
  void open_url(const std::string& url) override {
    const int wide_len = ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    if (wide_len <= 1) return;
    std::wstring wide(static_cast<size_t>(wide_len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), wide_len);
    ::ShellExecuteW(hwnd_, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }

  bool has_window() const override { return true; }
  void* native_window_handle() const override { return reinterpret_cast<void*>(hwnd_); }
  const char* backend_name() const override { return "win32 + OpenGL 3.2 core"; }

  HWND hwnd() const { return hwnd_; }

private:
  bool create_gl_context(std::string& error) {
    device_context_ = ::GetDC(hwnd_);
    if (!device_context_) {
      error = "GetDC failed";
      return false;
    }
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 0;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = ::ChoosePixelFormat(device_context_, &pfd);
    if (!format || !::SetPixelFormat(device_context_, format, &pfd)) {
      error = "no suitable pixel format";
      return false;
    }

    // Bootstrap context: the only way to reach wglCreateContextAttribsARB.
    const HGLRC bootstrap = ::wglCreateContext(device_context_);
    if (!bootstrap) {
      error = "wglCreateContext failed";
      return false;
    }
    if (!::wglMakeCurrent(device_context_, bootstrap)) {
      error = "wglMakeCurrent failed";
      ::wglDeleteContext(bootstrap);
      return false;
    }
    using CreateContextAttribs = HGLRC (*)(HDC, HGLRC, const int*);
    auto create_attribs = reinterpret_cast<CreateContextAttribs>(
        ::wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_attribs) {
      // Driver without GL 3.2 core support: keep the compatibility context.
      gl_context_ = bootstrap;
      ONOW_WARN("Platform", "wglCreateContextAttribsARB missing; using legacy context");
      return true;
    }
    const int attribs[] = {0x2091 /*WGL_CONTEXT_MAJOR_VERSION_ARB*/, 3,
                           0x2092 /*WGL_CONTEXT_MINOR_VERSION_ARB*/, 2,
                           0x9126 /*WGL_CONTEXT_PROFILE_MASK_ARB*/, 0x00000001,
                           0 /*WGL_CONTEXT_FLAGS_ARB*/, 0,
                           0x2094 /*WGL_CONTEXT_FLAGS_ARB*/, 0, 0};
    const HGLRC modern = create_attribs(device_context_, nullptr, attribs);
    if (!modern) {
      gl_context_ = bootstrap;
      ONOW_WARN("Platform", "OpenGL 3.2 core context refused; using legacy context");
      return true;
    }
    ::wglMakeCurrent(nullptr, nullptr);
    ::wglDeleteContext(bootstrap);
    gl_context_ = modern;
    if (!::wglMakeCurrent(device_context_, gl_context_)) {
      error = "wglMakeCurrent(modern) failed";
      return false;
    }
    if (!gl::load()) {
      error = "OpenGL 3.2 entry points missing on this driver";
      return false;
    }
    return true;
  }

  static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<Win32Platform*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return ::DefWindowProcW(hwnd, message, wparam, lparam);
    return self->handle_message(hwnd, message, wparam, lparam);
  }

  LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_CLOSE:
      case WM_DESTROY:
        quit_requested_ = true;
        ::PostQuitMessage(0);
        return 0;
      case WM_SIZE: {
        width_ = LOWORD(lparam);
        height_ = HIWORD(lparam);
        return 0;
      }
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN: {
        const Key key = translate_key(wparam, lparam);
        const int index = static_cast<int>(key);
        if (index > 0 && index < static_cast<int>(Key::Count)) {
          if (!input_.key_down[index]) input_.key_pressed[index] = true;
          input_.key_down[index] = true;
        }
        sync_modifiers();
        return 0;
      }
      case WM_KEYUP:
      case WM_SYSKEYUP: {
        const Key key = translate_key(wparam, lparam);
        const int index = static_cast<int>(key);
        if (index > 0 && index < static_cast<int>(Key::Count)) {
          input_.key_down[index] = false;
          input_.key_released[index] = true;
        }
        sync_modifiers();
        return 0;
      }
      case WM_CHAR: {
        // UTF-16 -> UTF-8, including surrogate pairs delivered as two messages.
        const wchar_t wide = static_cast<wchar_t>(wparam);
        if (wide >= 0xD800 && wide <= 0xDBFF) {
          pending_high_surrogate_ = wide;
          return 0;
        }
        wchar_t pair[3] = {wide, 0, 0};
        if (wide >= 0xDC00 && wide <= 0xDFFF && pending_high_surrogate_) {
          pair[0] = pending_high_surrogate_;
          pair[1] = wide;
          pending_high_surrogate_ = 0;
        }
        const int needed = ::WideCharToMultiByte(CP_UTF8, 0, pair, -1, nullptr, 0, nullptr, nullptr);
        if (needed > 1) {
          std::string utf8(static_cast<size_t>(needed - 1), '\0');
          ::WideCharToMultiByte(CP_UTF8, 0, pair, -1, utf8.data(), needed, nullptr, nullptr);
          input_.text_input += utf8;
        }
        return 0;
      }
      case WM_MOUSEMOVE: {
        const float x = static_cast<float>(LOWORD(lparam));
        const float y = static_cast<float>(HIWORD(lparam));
        if (pointer_locked_) {
          input_.mouse_dx += x - lock_origin_x_;
          input_.mouse_dy += y - lock_origin_y_;
        } else {
          input_.mouse_dx += x - input_.mouse_x;
          input_.mouse_dy += y - input_.mouse_y;
        }
        input_.mouse_x = x;
        input_.mouse_y = y;
        if (pointer_locked_) {
          lock_origin_x_ = x;
          lock_origin_y_ = y;
        }
        return 0;
      }
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN: {
        const int index = message == WM_LBUTTONDOWN ? 0 : (message == WM_RBUTTONDOWN ? 1 : 2);
        input_.mouse_down[index] = true;
        input_.mouse_pressed[index] = true;
        ::SetCapture(hwnd);
        return 0;
      }
      case WM_LBUTTONUP:
      case WM_RBUTTONUP:
      case WM_MBUTTONUP: {
        const int index = message == WM_LBUTTONUP ? 0 : (message == WM_RBUTTONUP ? 1 : 2);
        input_.mouse_down[index] = false;
        input_.mouse_released[index] = true;
        ::ReleaseCapture();
        return 0;
      }
      case WM_MOUSEWHEEL: {
        input_.wheel_y += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / 120.0f;
        return 0;
      }
      case WM_MOUSEHWHEEL: {
        input_.wheel_x += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / 120.0f;
        return 0;
      }
      case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
          set_cursor_shape(cursor_shape_);
          return TRUE;
        }
        break;
      default:
        break;
    }
    return ::DefWindowProcW(hwnd, message, wparam, lparam);
  }

  void sync_modifiers() {
    input_.ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    input_.shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    input_.alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
    input_.super = (::GetKeyState(VK_LWIN) & 0x8000) || (::GetKeyState(VK_RWIN) & 0x8000);
  }

  PlatformConfig config_;
  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HDC device_context_ = nullptr;
  HGLRC gl_context_ = nullptr;
  int width_ = 1280;
  int height_ = 720;
  bool fullscreen_ = false;
  bool quit_requested_ = false;
  bool cursor_visible_ = true;
  CursorShape cursor_shape_ = CursorShape::Arrow;
  bool pointer_locked_ = false;
  float lock_origin_x_ = 0.0f;
  float lock_origin_y_ = 0.0f;
  wchar_t pending_high_surrogate_ = 0;
  InputState input_;
};

Platform* create_platform() { return new Win32Platform(); }
void destroy_platform(Platform* platform) { delete platform; }

} // namespace onow

#else // !_WIN32

namespace onow {
Platform* create_platform() { return nullptr; }
void destroy_platform(Platform*) {}
} // namespace onow

#endif
