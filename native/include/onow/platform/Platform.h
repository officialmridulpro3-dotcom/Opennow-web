// The platform layer: window, OpenGL context, input, clipboard, cursor.
//
// Three backends implement this contract:
//   * Win32   — Win32 window + WGL/OpenGL 3.2 core context. Zero dependencies
//               beyond opengl32.lib, which ships with Windows.
//   * GLFW    — one code path for Linux/macOS/Windows with a GLFW package.
//   * Headless— no window at all. Drives the app with a synthetic clock and a
//               fixed framebuffer, which is what the unit tests and CI use.
//
// Nothing above this file may touch a windowing API directly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace onow {

enum class Key : int {
  Unknown = 0,
  Tab,
  LeftArrow,
  RightArrow,
  UpArrow,
  DownArrow,
  PageUp,
  PageDown,
  Home,
  End,
  Insert,
  Delete,
  Backspace,
  Space,
  Enter,
  Escape,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  Numpad0, Numpad1, Numpad2, Numpad3, Numpad4, Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
  LeftCtrl, LeftShift, LeftAlt, LeftSuper,
  RightCtrl, RightShift, RightAlt, RightSuper,
  Count,
};

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, Count = 3 };

struct PlatformConfig {
  std::string title = "OpenNOW";
  int width = 1440;
  int height = 900;
  int min_width = 960;
  int min_height = 600;
  bool resizable = true;
  bool start_maximized = false;
  bool vsync = true;
  // Headless only: frames rendered before the app is asked to quit.
  int max_frames = 0;
};

struct InputState {
  // Keyboard. `down` is sticky for the frame; `pressed`/`released` are edges.
  bool key_down[static_cast<int>(Key::Count)] = {false};
  bool key_pressed[static_cast<int>(Key::Count)] = {false};
  bool key_released[static_cast<int>(Key::Count)] = {false};
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  bool super = false;

  bool mouse_down[static_cast<int>(MouseButton::Count)] = {false};
  bool mouse_pressed[static_cast<int>(MouseButton::Count)] = {false};
  bool mouse_released[static_cast<int>(MouseButton::Count)] = {false};
  float mouse_x = 0.0f;
  float mouse_y = 0.0f;
  float mouse_dx = 0.0f; // accumulated since last frame (raw, pointer-lock aware)
  float mouse_dy = 0.0f;
  float wheel_x = 0.0f;
  float wheel_y = 0.0f;

  // UTF-8 text typed this frame (fed straight into ImGui's InputText).
  std::string text_input;
};

class Platform {
public:
  virtual ~Platform() = default;

  // Creates the window and the GL context. `error` is filled on failure.
  virtual bool init(const PlatformConfig& config, std::string& error) = 0;
  virtual void shutdown() = 0;

  // Pumps the event queue. Returns false once the user asked to quit.
  virtual bool poll_events() = 0;
  // Clears the backbuffer and resizes the viewport if the window changed.
  virtual void begin_frame() = 0;
  // Swaps buffers (and throttles to vsync where the platform supports it).
  virtual void end_frame() = 0;

  virtual void framebuffer_size(int& width, int& height) const = 0;
  virtual void window_size(int& width, int& height) const = 0;
  virtual float content_scale() const = 0;

  virtual const InputState& input() const = 0;

  virtual void set_title(const std::string& title) = 0;
  virtual void set_fullscreen(bool fullscreen) = 0;
  virtual bool is_fullscreen() const = 0;
  virtual bool is_focused() const = 0;

  // Clipboard (stream paste / copy support).
  virtual std::string clipboard_get_text() = 0;
  virtual void clipboard_set_text(const std::string& text) = 0;

  // Cursor. The stream engine hides the OS cursor and switches to relative
  // motion while the game has the pointer, exactly like the vendor client.
  enum class CursorShape { Arrow, TextInput, ResizeAll, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE, Hand, NotAllowed };
  virtual void set_cursor_shape(CursorShape shape) = 0;
  virtual void set_cursor_visible(bool visible) = 0;
  virtual void set_pointer_lock(bool locked) = 0;
  virtual bool pointer_locked() const = 0;

  // Open a native file picker. Returns false when the user cancelled.
  virtual bool open_file_dialog(const std::string& title, const std::string& filter,
                                std::string& out_path) = 0;
  virtual bool save_file_dialog(const std::string& title, const std::string& default_name,
                                const std::string& filter, std::string& out_path) = 0;

  // Opens the OS "open URL" handler. Best effort, never blocks.
  virtual void open_url(const std::string& url) = 0;

  // Whether this backend can actually present. False for headless.
  virtual bool has_window() const = 0;

  // Opaque native window handle (HWND on Windows, GLFWwindow* elsewhere,
  // nullptr when headless). Needed by the ImGui platform backends.
  virtual void* native_window_handle() const = 0;

  // Human readable backend name for Settings → About.
  virtual const char* backend_name() const = 0;
};

// Factory. Selects the backend the build was configured with.
Platform* create_platform();
void destroy_platform(Platform* platform);

} // namespace onow
