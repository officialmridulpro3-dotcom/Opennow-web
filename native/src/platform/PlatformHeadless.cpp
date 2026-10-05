// Headless platform: no window, no GL, a synthetic clock.
//
// Used by the unit tests and by `opennow --headless`, which renders N frames of
// the real UI into a discarded framebuffer. It exercises every code path that
// does not depend on the GPU: navigation, state machines, service callbacks,
// and the whole layout pass (which is where the styling lives).
#include "onow/platform/Platform.h"

#include "onow/Log.h"
#include "onow/Time.h"

#include <atomic>
#include <cstring>

namespace onow {

namespace {

const char* kKeyNames[static_cast<int>(Key::Count)] = {};

void init_key_names() {
  static bool done = false;
  if (done) return;
  done = true;
}

} // namespace

class HeadlessPlatform : public Platform {
public:
  bool init(const PlatformConfig& config, std::string& error) override {
    (void)error;
    config_ = config;
    width_ = config.width;
    height_ = config.height;
    frames_remaining_ = config.max_frames;
    started_ms_ = now_ms();
    ONOW_INFO("Platform", "headless backend initialised (%dx%d)", width_, height_);
    return true;
  }

  void shutdown() override {}

  bool poll_events() override {
    ++frames_rendered_;
    if (config_.max_frames > 0 && frames_rendered_ >= static_cast<uint64_t>(config_.max_frames)) {
      return false;
    }
    // Without a window there is nothing to pump; the caller drives time.
    return true;
  }

  void begin_frame() override {
    input_.key_pressed[0] = false;
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
  }

  void end_frame() override {
    if (config_.max_frames > 0 && frames_rendered_ >= static_cast<uint64_t>(config_.max_frames)) {
      quit_requested_ = true;
    }
  }

  void framebuffer_size(int& width, int& height) const override {
    width = width_;
    height = height_;
  }
  void window_size(int& width, int& height) const override {
    width = width_;
    height = height_;
  }
  float content_scale() const override { return 1.0f; }

  const InputState& input() const override { return input_; }

  void set_title(const std::string& title) override { title_ = title; }
  void set_fullscreen(bool fullscreen) override { fullscreen_ = fullscreen; }
  bool is_fullscreen() const override { return fullscreen_; }
  bool is_focused() const override { return true; }

  std::string clipboard_get_text() override { return clipboard_; }
  void clipboard_set_text(const std::string& text) override { clipboard_ = text; }

  void set_cursor_shape(CursorShape shape) override { cursor_shape_ = shape; }
  void set_cursor_visible(bool visible) override { cursor_visible_ = visible; }
  void set_pointer_lock(bool locked) override { pointer_locked_ = locked; }
  bool pointer_locked() const override { return pointer_locked_; }

  bool open_file_dialog(const std::string& title, const std::string& filter,
                        std::string& out_path) override {
    (void)title;
    (void)filter;
    out_path.clear();
    return false;
  }
  bool save_file_dialog(const std::string& title, const std::string& default_name,
                        const std::string& filter, std::string& out_path) override {
    (void)title;
    (void)default_name;
    (void)filter;
    out_path.clear();
    return false;
  }
  void open_url(const std::string& url) override {
    ONOW_INFO("Platform", "open_url (headless): %s", url.c_str());
  }

  bool has_window() const override { return false; }
  void* native_window_handle() const override { return nullptr; }
  const char* backend_name() const override { return "headless"; }

  // Test hooks.
  void inject_key(Key key, bool down) {
    const int index = static_cast<int>(key);
    if (index <= 0 || index >= static_cast<int>(Key::Count)) return;
    input_.key_down[index] = down;
    if (down) input_.key_pressed[index] = true;
    else input_.key_released[index] = true;
  }
  void inject_text(const std::string& text) { input_.text_input += text; }
  void inject_mouse(float x, float y) {
    input_.mouse_dx += x;
    input_.mouse_dy += y;
    input_.mouse_x += x;
    input_.mouse_y += y;
  }
  void inject_click(MouseButton button) {
    const int index = static_cast<int>(button);
    input_.mouse_down[index] = true;
    input_.mouse_pressed[index] = true;
  }
  void resize(int width, int height) {
    width_ = width;
    height_ = height;
  }
  uint64_t frames_rendered() const { return frames_rendered_; }
  bool quit_requested() const { return quit_requested_; }

private:
  PlatformConfig config_;
  int width_ = 1280;
  int height_ = 720;
  std::string title_;
  bool fullscreen_ = false;
  std::string clipboard_;
  CursorShape cursor_shape_ = CursorShape::Arrow;
  bool cursor_visible_ = true;
  bool pointer_locked_ = false;
  bool quit_requested_ = false;
  InputState input_;
  uint64_t frames_rendered_ = 0;
  uint64_t frames_remaining_ = 0;
  int64_t started_ms_ = 0;
};

Platform* create_platform() {
  init_key_names();
  return new HeadlessPlatform();
}

void destroy_platform(Platform* platform) { delete platform; }

} // namespace onow
