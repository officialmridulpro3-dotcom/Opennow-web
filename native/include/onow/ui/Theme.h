// The OpenNOW design system, ported from `src/client/styles.css`.
//
// The web client's token block is the contract this file reproduces: six
// approved colours, four type scales, one shadow, chamfers instead of pills,
// dark ground only. Nothing here invents a colour — every value is either
// copied from the CSS `:root` block or derived from it with the same
// `color-mix` semantics the browser used.
//
//   --color-void  #000000      ground
//   --color-ultra #0000ee      the blue that tints every layered surface
//   --color-steel #9b99ad      ink and borders
//   --color-azure #00a3ff      attention (the palette carries no red)
//   --color-neon  #76ff3b      accent + success
//   --color-light #ffffff      primary ink
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"

namespace onow {

struct Rgb {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

Rgb rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
Rgb rgba(uint32_t value);          // 0xRRGGBBAA
Rgb mix(const Rgb& a, const Rgb& b, float t); // t=0 -> a, t=1 -> b
ImU32 col(const Rgb& c);
ImVec4 vec4(const Rgb& c);

struct Palette {
  // Ground — layered blue-black, never flat.
  Rgb void_;
  Rgb bg_a;
  Rgb bg_b;
  Rgb bg_c;

  // Panels & cards.
  Rgb panel;
  Rgb panel_border;
  Rgb panel_border_solid;
  Rgb card;
  Rgb card_hover;
  Rgb card_selected;
  Rgb chip;

  // Ink.
  Rgb ink;
  Rgb ink_soft;
  Rgb ink_muted;

  // Accent (switchable) + semantic.
  Rgb accent;
  Rgb accent_hover;
  Rgb accent_press;
  Rgb accent_glow;
  Rgb accent_surface;
  Rgb accent_surface_strong;
  Rgb accent_on;
  Rgb error;
  Rgb error_bg;
  Rgb error_border;
  Rgb warning;
  Rgb info;
  Rgb success;

  Rgb shadow;
};

// Accent presets, matching `settings.appAccentColor` in the web client.
enum class AccentPreset { Green, Blue, Violet, Amber, Rose };

Palette build_palette(AccentPreset preset);
Palette& active_palette();
void set_active_palette(AccentPreset preset);

// Applies the palette to an ImGuiStyle: rounding, spacing, and every colour
// slot the widgets use. Called once at startup and again whenever the accent
// changes (the web client did the same through CSS custom properties).
void apply_to_imgui(ImGuiStyle& style, const Palette& palette, float scale);

// Layout metrics from the CSS (`--navbar-h`, `--rail-w`, ...).
struct Metrics {
  float navbar_height;
  float rail_width;
  float header_height;
  float statusbar_height;
  float content_x;
  float radius_sm;
  float radius_md;
  float radius_lg;
  float scrollbar_size;
};

const Metrics& metrics();

// ---------------------------------------------------------------- drawing ---

// A chamfered panel: the deck's signature shape (a rectangle with two clipped
// corners) rather than a rounded rect. `chamfer` is in pixels.
void draw_panel(ImDrawList* list, const ImVec2& min, const ImVec2& max, const Rgb& fill,
                const Rgb& border, float chamfer, float rounding = 0.0f,
                float border_thickness = 1.0f);

// The single extracted shadow: `6px 5px 12px 3px rgba(29,28,28,0.21)`.
void draw_shadow(ImDrawList* list, const ImVec2& min, const ImVec2& max, float rounding = 0.0f);

// Neon glow, used for focus rings and the accent underline.
void draw_glow(ImDrawList* list, const ImVec2& center, float radius, const Rgb& color,
               float strength = 0.25f);

// Horizontal gradient across a rect (the catalog atmosphere).
void draw_gradient(ImDrawList* list, const ImVec2& min, const ImVec2& max, const Rgb& left,
                   const Rgb& right);

// Type scale. Fonts are baked into the atlas at these sizes because Dear ImGui
// has no runtime font scaling, so the four steps the web design uses (11/14/18/
// 24 px) become four atlas entries and the widgets pick one by pointer.
struct TypeScale {
  ImFont* caption = nullptr; // 11 px — eyebrows, meta rows
  ImFont* body = nullptr;    // 14 px — the CSS body size
  ImFont* title = nullptr;   // 18 px — card and section titles
  ImFont* display = nullptr; // 24 px — page titles
  ImFont* mono = nullptr;    // 14 px mono — stats and diagnostics
};

void set_type_scale(const TypeScale& scale);
const TypeScale& type_scale();

// Text helpers that honour the palette's ink hierarchy.
void text(const char* s, const Rgb& color, ImFont* font = nullptr);
void text_at(ImDrawList* list, const ImVec2& pos, const Rgb& color, const char* s,
             ImFont* font = nullptr);
ImVec2 measure_text(const char* s, ImFont* font = nullptr);

// Truncates with an ellipsis to `max_width`, in pixels.
std::string ellipsize(const std::string& text, float max_width, ImFont* font = nullptr);

} // namespace onow
