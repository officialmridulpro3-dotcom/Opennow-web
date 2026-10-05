// Glue between the platform layer and Dear ImGui, plus the font atlas.
//
// The web design names four type steps (11 / 14 / 18 / 24 px) and four
// families. Dear ImGui has no runtime font scaling, so each step is baked into
// the atlas as its own ImFont and the widgets pick one by pointer. Fonts are
// loaded from the system font directory when present and fall back to ImGui's
// built-in proportional font, so the binary carries no multi-megabyte payload.
#pragma once

#include "onow/platform/Platform.h"
#include "onow/ui/Theme.h"

namespace onow {

class ImGuiLayer {
public:
  // `install_backend` links the platform imgui_impl_* translation units. The
  // headless backend passes false: there is no window to translate events for.
  bool init(Platform& platform, bool install_backend);
  void shutdown();

  // Call after poll_events() and before any UI code.
  void new_frame();
  // Renders and (for windowed backends) presents.
  void render();

  float content_scale() const { return content_scale_; }
  const TypeScale& fonts() const { return fonts_; }

private:
  void* load_font(const char* path, float size_pixels);
  void load_family(const char* const* paths, size_t count, float size, ImFont** out);

  Platform* platform_ = nullptr;
  TypeScale fonts_;
  float content_scale_ = 1.0f;
  bool backend_installed_ = false;
  bool initialized_ = false;
};

} // namespace onow
