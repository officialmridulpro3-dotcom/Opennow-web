// GLFW platform backend: one code path for Linux, macOS and BSD.
//
// This is the windowed build (`make PLATFORM=glfw`). GLFW owns the window, the
// OpenGL 3.2 core context and the event queue; everything above this file only
// sees the Platform contract. The GL entry points come from gfx/GLLoader.h, so
// the build needs nothing but `-lglfw -lGL`.
//
// The ImGui GLFW + OpenGL3 backends (third_party/imgui/backends) are linked in
// for this platform and translate these same events into ImGui input.
#include "onow/platform/Platform.h"

#include "onow/Log.h"
#include "onow/gfx/GLLoader.h"

#if !defined(ONOW_PLATFORM_GLFW)
// Compiled by mistake for another platform: stay silent and let the selected
// backend provide create_platform().
#else

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <unistd.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace onow {

namespace {

// ------------------------------------------------------------------ helpers --

Key key_from_glfw(int key) {
  switch (key) {
    case GLFW_KEY_TAB: return Key::Tab;
    case GLFW_KEY_LEFT: return Key::LeftArrow;
    case GLFW_KEY_RIGHT: return Key::RightArrow;
    case GLFW_KEY_UP: return Key::UpArrow;
    case GLFW_KEY_DOWN: return Key::DownArrow;
    case GLFW_KEY_PAGE_UP: return Key::PageUp;
    case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
    case GLFW_KEY_HOME: return Key::Home;
    case GLFW_KEY_END: return Key::End;
    case GLFW_KEY_INSERT: return Key::Insert;
    case GLFW_KEY_DELETE: return Key::Delete;
    case GLFW_KEY_BACKSPACE: return Key::Backspace;
    case GLFW_KEY_SPACE: return Key::Space;
    case GLFW_KEY_ENTER:
    case GLFW_KEY_KP_ENTER: return Key::Enter;
    case GLFW_KEY_ESCAPE: return Key::Escape;
    case GLFW_KEY_A: return Key::A;
    case GLFW_KEY_B: return Key::B;
    case GLFW_KEY_C: return Key::C;
    case GLFW_KEY_D: return Key::D;
    case GLFW_KEY_E: return Key::E;
    case GLFW_KEY_F: return Key::F;
    case GLFW_KEY_G: return Key::G;
    case GLFW_KEY_H: return Key::H;
    case GLFW_KEY_I: return Key::I;
    case GLFW_KEY_J: return Key::J;
    case GLFW_KEY_K: return Key::K;
    case GLFW_KEY_L: return Key::L;
    case GLFW_KEY_M: return Key::M;
    case GLFW_KEY_N: return Key::N;
    case GLFW_KEY_O: return Key::O;
    case GLFW_KEY_P: return Key::P;
    case GLFW_KEY_Q: return Key::Q;
    case GLFW_KEY_R: return Key::R;
    case GLFW_KEY_S: return Key::S;
    case GLFW_KEY_T: return Key::T;
    case GLFW_KEY_U: return Key::U;
    case GLFW_KEY_V: return Key::V;
    case GLFW_KEY_W: return Key::W;
    case GLFW_KEY_X: return Key::X;
    case GLFW_KEY_Y: return Key::Y;
    case GLFW_KEY_Z: return Key::Z;
    case GLFW_KEY_0: return Key::Num0;
    case GLFW_KEY_1: return Key::Num1;
    case GLFW_KEY_2: return Key::Num2;
    case GLFW_KEY_3: return Key::Num3;
    case GLFW_KEY_4: return Key::Num4;
    case GLFW_KEY_5: return Key::Num5;
    case GLFW_KEY_6: return Key::Num6;
    case GLFW_KEY_7: return Key::Num7;
    case GLFW_KEY_8: return Key::Num8;
    case GLFW_KEY_9: return Key::Num9;
    case GLFW_KEY_KP_0: return Key::Numpad0;
    case GLFW_KEY_KP_1: return Key::Numpad1;
    case GLFW_KEY_KP_2: return Key::Numpad2;
    case GLFW_KEY_KP_3: return Key::Numpad3;
    case GLFW_KEY_KP_4: return Key::Numpad4;
    case GLFW_KEY_KP_5: return Key::Numpad5;
    case GLFW_KEY_KP_6: return Key::Numpad6;
    case GLFW_KEY_KP_7: return Key::Numpad7;
    case GLFW_KEY_KP_8: return Key::Numpad8;
    case GLFW_KEY_KP_9: return Key::Numpad9;
    case GLFW_KEY_F1: return Key::F1;
    case GLFW_KEY_F2: return Key::F2;
    case GLFW_KEY_F3: return Key::F3;
    case GLFW_KEY_F4: return Key::F4;
    case GLFW_KEY_F5: return Key::F5;
    case GLFW_KEY_F6: return Key::F6;
    case GLFW_KEY_F7: return Key::F7;
    case GLFW_KEY_F8: return Key::F8;
    case GLFW_KEY_F9: return Key::F9;
    case GLFW_KEY_F10: return Key::F10;
    case GLFW_KEY_F11: return Key::F11;
    case GLFW_KEY_F12: return Key::F12;
    case GLFW_KEY_LEFT_CONTROL: return Key::LeftCtrl;
    case GLFW_KEY_RIGHT_CONTROL: return Key::RightCtrl;
    case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
    case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
    case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
    case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
    case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
    case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
    default: return Key::Unknown;
  }
}

int mouse_button_from_glfw(int button) {
  if (button == GLFW_MOUSE_BUTTON_LEFT) return static_cast<int>(MouseButton::Left);
  if (button == GLFW_MOUSE_BUTTON_RIGHT) return static_cast<int>(MouseButton::Right);
  if (button == GLFW_MOUSE_BUTTON_MIDDLE) return static_cast<int>(MouseButton::Middle);
  return -1;
}

// Single-quote a value for a shell command line.
std::string shell_quote(const std::string& value) {
  std::string out = "'";
  for (char c : value) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

// Runs a dialog helper and returns its stdout with the trailing newline gone.
// Returns an empty string when the helper is missing or the user cancelled.
std::string run_helper(const std::string& command) {
  std::string out;
#if defined(_WIN32)
  FILE* pipe = _popen(command.c_str(), "r");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (!pipe) return out;
  char buffer[1024];
  while (std::fgets(buffer, sizeof(buffer), pipe)) out += buffer;
#if defined(_WIN32)
  _pclose(pipe);
#else
  pclose(pipe);
#endif
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
  return out;
}

bool helper_exists(const char* name) {
#if defined(_WIN32)
  return false;
#else
  const char* path = std::getenv("PATH");
  if (!path) return false;
  const std::string search(path);
  std::string::size_type start = 0;
  while (start <= search.size()) {
    std::string::size_type end = search.find(':', start);
    if (end == std::string::npos) end = search.size();
    const std::string dir = search.substr(start, end - start);
    if (!dir.empty()) {
      const std::string candidate = dir + "/" + name;
      if (::access(candidate.c_str(), X_OK) == 0) return true;
    }
    if (end == search.size()) break;
    start = end + 1;
  }
  return false;
#endif
}

// Launches a URL without blocking the UI thread.
void spawn_detached(const std::string& command) {
#if defined(_WIN32)
  ShellExecuteA(nullptr, "open", command.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  const pid_t pid = ::fork();
  if (pid == 0) {
    ::execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
    ::_exit(127);
  }
  // The parent deliberately does not wait: xdg-open can take seconds to hand
  // the URL to a browser that is still starting up.
  if (pid > 0) {
    int status = 0;
    ::waitpid(pid, &status, WNOHANG);
  }
#endif
}

std::string pick_file(bool save, const std::string& title, const std::string& default_name) {
#if defined(_WIN32)
  char buffer[MAX_PATH] = {0};
  if (!default_name.empty() && default_name.size() < MAX_PATH) {
    std::memcpy(buffer, default_name.data(), default_name.size());
  }
  OPENFILENAMEA ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "All Files\0*.*\0";
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = sizeof(buffer);
  ofn.lpstrTitle = title.c_str();
  ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
  return ok ? std::string(buffer) : std::string();
#elif defined(__APPLE__)
  const std::string script =
      save ? "POSIX path of (choose file name with prompt " + shell_quote(title) + ")"
           : "POSIX path of (choose file with prompt " + shell_quote(title) + ")";
  return run_helper("osascript -e " + shell_quote(script));
#else
  if (helper_exists("zenity")) {
    return run_helper("zenity --file-selection " +
                      std::string(save ? "--save --confirm-overwrite " : "") +
                      "--title " + shell_quote(title) + " --filename " +
                      shell_quote(default_name));
  }
  if (helper_exists("kdialog")) {
    return run_helper("kdialog --get" + std::string(save ? "savefilename" : "openfilename") +
                      " " + shell_quote(default_name.empty() ? "." : default_name) + " --title " +
                      shell_quote(title));
  }
  if (helper_exists("matedialog")) {
    return run_helper("matedialog --file-selection --title " + shell_quote(title));
  }
  ONOW_WARN("Platform", "no file dialog helper found (install zenity or kdialog)");
  return std::string();
#endif
}

} // namespace

// ------------------------------------------------------------------ backend --

class GlfwPlatform : public Platform {
public:
  bool init(const PlatformConfig& config, std::string& error) override {
    config_ = config;
    if (!glfwInit()) {
      error = "glfwInit failed";
      return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    window_ = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!window_) {
      error = "glfwCreateWindow failed (no GL 3.2 core context available)";
      glfwTerminate();
      return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(config.vsync ? 1 : 0);

    // The window must own a current context before the entry points resolve.
    if (!gl::load()) {
      const char* missing = nullptr;
      gl::load(&missing);
      error = std::string("OpenGL entry point missing: ") + (missing ? missing : "?");
      shutdown();
      return false;
    }
    const char* version = reinterpret_cast<const char*>(gl::GETSTRING(0x1F02 /* GL_VERSION */));
    ONOW_INFO("Platform", "GLFW backend initialised (%dx%d, GL %s)", config.width, config.height,
              version ? version : "?");

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, &GlfwPlatform::on_framebuffer_resize);
    glfwSetKeyCallback(window_, &GlfwPlatform::on_key);
    glfwSetCharCallback(window_, &GlfwPlatform::on_char);
    glfwSetMouseButtonCallback(window_, &GlfwPlatform::on_mouse_button);
    glfwSetCursorPosCallback(window_, &GlfwPlatform::on_cursor_pos);
    glfwSetScrollCallback(window_, &GlfwPlatform::on_scroll);
    glfwSetWindowFocusCallback(window_, &GlfwPlatform::on_focus);
    glfwSetWindowCloseCallback(window_, &GlfwPlatform::on_close);

    if (config.start_maximized) glfwMaximizeWindow(window_);
    glfwGetFramebufferSize(window_, &fb_width_, &fb_height_);
    return true;
  }

  void shutdown() override {
    if (window_) {
      glfwDestroyWindow(window_);
      window_ = nullptr;
    }
    for (auto& entry : cursors_) {
      if (entry.second) glfwDestroyCursor(entry.second);
    }
    cursors_.clear();
    glfwTerminate();
  }

  bool poll_events() override {
    if (quit_requested_) return false;
    glfwPollEvents();
    if (glfwWindowShouldClose(window_)) return false;
    return true;
  }

  void begin_frame() override {
    std::memset(input_.key_pressed, 0, sizeof(input_.key_pressed));
    std::memset(input_.key_released, 0, sizeof(input_.key_released));
    for (int i = 0; i < static_cast<int>(MouseButton::Count); ++i) {
      input_.mouse_pressed[i] = false;
      input_.mouse_released[i] = false;
    }
    input_.mouse_dx = 0.0f;
    input_.mouse_dy = 0.0f;
    input_.wheel_x = 0.0f;
    input_.wheel_y = 0.0f;
    input_.text_input.clear();

    glfwGetFramebufferSize(window_, &fb_width_, &fb_height_);
    gl::VIEWPORT(0, 0, fb_width_, fb_height_);
    gl::CLEARCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    gl::CLEAR(0x00004000 /* GL_COLOR_BUFFER_BIT */);
  }

  void end_frame() override { glfwSwapBuffers(window_); }

  void framebuffer_size(int& width, int& height) const override {
    width = fb_width_;
    height = fb_height_;
  }
  void window_size(int& width, int& height) const override {
    glfwGetWindowSize(window_, &width, &height);
  }
  float content_scale() const override {
    float x = 1.0f;
    float y = 1.0f;
    glfwGetWindowContentScale(window_, &x, &y);
    return x > 0.0f ? x : 1.0f;
  }

  const InputState& input() const override { return input_; }

  void set_title(const std::string& title) override { glfwSetWindowTitle(window_, title.c_str()); }

  void set_fullscreen(bool fullscreen) override {
    if (fullscreen == fullscreen_) return;
    fullscreen_ = fullscreen;
    if (fullscreen) {
      glfwGetWindowSize(window_, &windowed_width_, &windowed_height_);
      glfwGetWindowPos(window_, &windowed_x_, &windowed_y_);
      GLFWmonitor* monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode* mode = glfwGetVideoMode(monitor);
      glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
      glfwSetWindowMonitor(window_, nullptr, windowed_x_, windowed_y_, windowed_width_,
                           windowed_height_, 0);
    }
  }
  bool is_fullscreen() const override { return fullscreen_; }
  bool is_focused() const override { return glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0; }

  std::string clipboard_get_text() override {
    const char* text = glfwGetClipboardString(window_);
    return text ? std::string(text) : std::string();
  }
  void clipboard_set_text(const std::string& text) override {
    glfwSetClipboardString(window_, text.c_str());
  }

  void set_cursor_shape(CursorShape shape) override {
    cursor_shape_ = shape;
    if (!cursor_visible_ || pointer_locked_) return;
    apply_cursor();
  }
  void set_cursor_visible(bool visible) override {
    cursor_visible_ = visible;
    if (visible) {
      glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
      apply_cursor();
    } else if (pointer_locked_) {
      glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
      glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
  }
  void set_pointer_lock(bool locked) override {
    pointer_locked_ = locked;
    glfwSetInputMode(window_, GLFW_CURSOR,
                     locked ? GLFW_CURSOR_DISABLED
                            : (cursor_visible_ ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN));
    if (!locked) apply_cursor();
  }
  bool pointer_locked() const override { return pointer_locked_; }

  bool open_file_dialog(const std::string& title, const std::string& filter,
                        std::string& out_path) override {
    (void)filter; // the helpers have no portable filter syntax; see pick_file()
    out_path = pick_file(false, title, std::string());
    return !out_path.empty();
  }
  bool save_file_dialog(const std::string& title, const std::string& default_name,
                        const std::string& filter, std::string& out_path) override {
    (void)filter;
    out_path = pick_file(true, title, default_name);
    return !out_path.empty();
  }

  void open_url(const std::string& url) override {
    if (url.empty()) return;
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    spawn_detached("open " + shell_quote(url));
#else
    if (helper_exists("xdg-open")) spawn_detached("xdg-open " + shell_quote(url));
    else ONOW_WARN("Platform", "xdg-open not found; cannot open %s", url.c_str());
#endif
  }

  bool has_window() const override { return window_ != nullptr; }
  void* native_window_handle() const override { return window_; }
  const char* backend_name() const override { return "glfw"; }

private:
  // ------------------------------------------------------------ glfw wiring --

  static void on_framebuffer_resize(GLFWwindow* window, int width, int height) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->fb_width_ = width;
      self->fb_height_ = height;
    }
  }

  static void on_key(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->on_key(key, action, mods);
    }
  }
  static void on_char(GLFWwindow* window, unsigned int codepoint) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->on_char(codepoint);
    }
  }
  static void on_mouse_button(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->on_mouse_button(button, action);
    }
  }
  static void on_cursor_pos(GLFWwindow* window, double x, double y) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->on_cursor_pos(x, y);
    }
  }
  static void on_scroll(GLFWwindow* window, double dx, double dy) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->on_scroll(dx, dy);
    }
  }
  static void on_focus(GLFWwindow* window, int focused) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->focused_ = focused != 0;
    }
  }
  static void on_close(GLFWwindow* window) {
    if (auto* self = static_cast<GlfwPlatform*>(glfwGetWindowUserPointer(window))) {
      self->quit_requested_ = true;
    }
  }

  void on_key(int key, int action, int mods) {
    const Key mapped = key_from_glfw(key);
    const bool down = action != GLFW_RELEASE;
    input_.ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    input_.shift = (mods & GLFW_MOD_SHIFT) != 0;
    input_.alt = (mods & GLFW_MOD_ALT) != 0;
    input_.super = (mods & GLFW_MOD_SUPER) != 0;
    if (mapped == Key::Unknown) return;
    const int index = static_cast<int>(mapped);
    if (down && !input_.key_down[index]) input_.key_pressed[index] = true;
    if (!down && input_.key_down[index]) input_.key_released[index] = true;
    input_.key_down[index] = down;
  }

  void on_char(unsigned int codepoint) {
    // Encode as UTF-8 so non-Latin layouts reach ImGui's text input intact.
    if (codepoint < 0x80) {
      input_.text_input += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
      input_.text_input += static_cast<char>(0xC0 | (codepoint >> 6));
      input_.text_input += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
      input_.text_input += static_cast<char>(0xE0 | (codepoint >> 12));
      input_.text_input += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      input_.text_input += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
      input_.text_input += static_cast<char>(0xF0 | (codepoint >> 18));
      input_.text_input += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
      input_.text_input += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
      input_.text_input += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
  }

  void on_mouse_button(int button, int action) {
    const int index = mouse_button_from_glfw(button);
    if (index < 0) return;
    const bool down = action != GLFW_RELEASE;
    if (down && !input_.mouse_down[index]) input_.mouse_pressed[index] = true;
    if (!down && input_.mouse_down[index]) input_.mouse_released[index] = true;
    input_.mouse_down[index] = down;
  }

  void on_cursor_pos(double x, double y) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    input_.mouse_dx += fx - cursor_x_;
    input_.mouse_dy += fy - cursor_y_;
    cursor_x_ = fx;
    cursor_y_ = fy;
    // While the pointer is locked the OS cursor is frozen at the centre, so the
    // virtual position is meaningless; keep the last real position instead.
    if (!pointer_locked_) {
      input_.mouse_x = fx;
      input_.mouse_y = fy;
    }
  }

  void on_scroll(double dx, double dy) {
    input_.wheel_x += static_cast<float>(dx);
    input_.wheel_y += static_cast<float>(dy);
  }

  void apply_cursor() {
    int standard = GLFW_ARROW_CURSOR;
    switch (cursor_shape_) {
      case CursorShape::Arrow: standard = GLFW_ARROW_CURSOR; break;
      case CursorShape::TextInput: standard = GLFW_IBEAM_CURSOR; break;
      case CursorShape::ResizeAll: standard = GLFW_ARROW_CURSOR; break;
      case CursorShape::ResizeNS: standard = GLFW_VRESIZE_CURSOR; break;
      case CursorShape::ResizeEW: standard = GLFW_HRESIZE_CURSOR; break;
      // GLFW 3.3 has no diagonal resize cursors; a hand reads as "grabbable".
      case CursorShape::ResizeNESW:
      case CursorShape::ResizeNWSE:
      case CursorShape::Hand: standard = GLFW_POINTING_HAND_CURSOR; break;
      case CursorShape::NotAllowed: standard = GLFW_ARROW_CURSOR; break;
    }
    auto found = cursors_.find(standard);
    if (found == cursors_.end()) {
      GLFWcursor* created = glfwCreateStandardCursor(standard);
      found = cursors_.emplace(standard, created).first;
    }
    if (found->second) glfwSetCursor(window_, found->second);
  }

  PlatformConfig config_;
  GLFWwindow* window_ = nullptr;
  int fb_width_ = 1280;
  int fb_height_ = 720;
  int windowed_width_ = 1280;
  int windowed_height_ = 720;
  int windowed_x_ = 0;
  int windowed_y_ = 0;
  float cursor_x_ = 0.0f;
  float cursor_y_ = 0.0f;
  bool fullscreen_ = false;
  bool focused_ = true;
  bool cursor_visible_ = true;
  bool pointer_locked_ = false;
  bool quit_requested_ = false;
  CursorShape cursor_shape_ = CursorShape::Arrow;
  InputState input_;
  std::map<int, GLFWcursor*> cursors_;
};

Platform* create_platform() { return new GlfwPlatform(); }

void destroy_platform(Platform* platform) { delete platform; }

} // namespace onow

#endif // ONOW_PLATFORM_GLFW
