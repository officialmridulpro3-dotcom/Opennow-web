#include "onow/ui/Theme.h"

#include "onow/Str.h"

#include <cmath>

namespace onow {

Rgb rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return Rgb{r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
}

Rgb rgba(uint32_t value) {
  return rgb(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
             static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
}

Rgb mix(const Rgb& a, const Rgb& b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  Rgb out;
  out.r = a.r + (b.r - a.r) * t;
  out.g = a.g + (b.g - a.g) * t;
  out.b = a.b + (b.b - a.b) * t;
  out.a = a.a + (b.a - a.a) * t;
  return out;
}

ImU32 col(const Rgb& c) { return ImGui::ColorConvertFloat4ToU32(vec4(c)); }

ImVec4 vec4(const Rgb& c) { return ImVec4(c.r, c.g, c.b, c.a); }

namespace {

// The six approved colours, straight from `:root`.
Rgb color_void() { return rgb(0x00, 0x00, 0x00); }
Rgb color_ultra() { return rgb(0x00, 0x00, 0xEE); }
Rgb color_steel() { return rgb(0x9B, 0x99, 0xAD); }
Rgb color_azure() { return rgb(0x00, 0xA3, 0xFF); }
Rgb color_neon() { return rgb(0x76, 0xFF, 0x3B); }
Rgb color_light() { return rgb(0xFF, 0xFF, 0xFF); }

Rgb accent_color(AccentPreset preset) {
  switch (preset) {
    case AccentPreset::Green: return color_neon();
    case AccentPreset::Blue: return color_azure();
    case AccentPreset::Violet: return rgb(0xA8, 0x7B, 0xFF);
    case AccentPreset::Amber: return rgb(0xFF, 0xB8, 0x3B);
    case AccentPreset::Rose: return rgb(0xFF, 0x5C, 0x8A);
  }
  return color_neon();
}

Rgb with_alpha(const Rgb& c, float a) {
  Rgb out = c;
  out.a = a;
  return out;
}

Palette g_palette;

} // namespace

Palette build_palette(AccentPreset preset) {
  Palette p;
  const Rgb void_ = color_void();
  const Rgb ultra = color_ultra();
  const Rgb steel = color_steel();
  const Rgb neon = accent_color(preset);

  p.void_ = void_;

  // Ground: `color-mix(in srgb, var(--color-ultra) 9%, var(--color-void))`.
  p.bg_a = void_;
  p.bg_b = mix(void_, ultra, 0.09f);
  p.bg_c = mix(void_, ultra, 0.16f);

  // Panels & cards.
  p.panel = mix(void_, ultra, 0.07f);
  p.panel_border = with_alpha(steel, 0.22f);
  p.panel_border_solid = mix(void_, steel, 0.34f);
  p.card = with_alpha(steel, 0.08f);
  p.card_hover = with_alpha(steel, 0.14f);
  p.card_selected = with_alpha(neon, 0.12f);
  p.chip = with_alpha(steel, 0.12f);

  // Ink.
  p.ink = color_light();
  p.ink_soft = steel;
  p.ink_muted = mix(steel, color_light(), 0.20f);

  // Accent.
  p.accent = neon;
  p.accent_hover = mix(neon, color_light(), 0.18f);
  p.accent_press = mix(neon, void_, 0.18f);
  p.accent_glow = with_alpha(neon, 0.25f);
  p.accent_surface = with_alpha(neon, 0.08f);
  p.accent_surface_strong = with_alpha(neon, 0.14f);
  p.accent_on = void_;

  // Semantic — the palette carries no red, so attention is azure.
  p.error = color_azure();
  p.error_bg = with_alpha(color_azure(), 0.10f);
  p.error_border = with_alpha(color_azure(), 0.32f);
  p.warning = color_azure();
  p.info = color_azure();
  p.success = neon;

  // The one extracted shadow.
  p.shadow = rgba(0x1D1C1C36);
  return p;
}

Palette& active_palette() { return g_palette; }

void set_active_palette(AccentPreset preset) { g_palette = build_palette(preset); }

void apply_to_imgui(ImGuiStyle& style, const Palette& p, float scale) {
  style = ImGuiStyle();
  style.WindowPadding = ImVec2(16.0f * scale, 14.0f * scale);
  style.FramePadding = ImVec2(12.0f * scale, 8.0f * scale);
  style.CellPadding = ImVec2(8.0f * scale, 6.0f * scale);
  style.ItemSpacing = ImVec2(10.0f * scale, 8.0f * scale);
  style.ItemInnerSpacing = ImVec2(6.0f * scale, 4.0f * scale);
  style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
  style.IndentSpacing = 20.0f * scale;
  style.ScrollbarSize = 5.0f * scale; // the CSS uses 5px scrollbars
  style.GrabMinSize = 12.0f * scale;

  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;

  // Radii — the detected set: chamfered clip-paths carry the deck shapes, so
  // ImGui rounding stays small and the custom widgets do the shaping.
  style.WindowRounding = 6.0f * scale;
  style.ChildRounding = 6.0f * scale;
  style.FrameRounding = 5.0f * scale;
  style.PopupRounding = 6.0f * scale;
  style.ScrollbarRounding = 3.0f * scale;
  style.GrabRounding = 3.0f * scale;
  style.TabRounding = 5.0f * scale;

  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
  style.DisplayWindowPadding = ImVec2(16.0f * scale, 14.0f * scale);
  style.DisplaySafeAreaPadding = ImVec2(4.0f * scale, 4.0f * scale);
  style.AntiAliasedLines = true;
  style.AntiAliasedLinesUseTex = true;
  style.AntiAliasedFill = true;
  style.CurveTessellationTol = 1.25f;

  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = vec4(p.ink);
  c[ImGuiCol_TextDisabled] = vec4(p.ink_muted);
  c[ImGuiCol_WindowBg] = vec4(p.bg_a);
  c[ImGuiCol_ChildBg] = vec4(with_alpha(p.panel, 0.0f));
  c[ImGuiCol_PopupBg] = vec4(p.panel);
  c[ImGuiCol_Border] = vec4(p.panel_border);
  c[ImGuiCol_BorderShadow] = vec4(with_alpha(p.void_, 0.0f));
  c[ImGuiCol_FrameBg] = vec4(p.card);
  c[ImGuiCol_FrameBgHovered] = vec4(p.card_hover);
  c[ImGuiCol_FrameBgActive] = vec4(p.card_selected);
  c[ImGuiCol_TitleBg] = vec4(p.panel);
  c[ImGuiCol_TitleBgActive] = vec4(p.panel);
  c[ImGuiCol_TitleBgCollapsed] = vec4(p.panel);
  c[ImGuiCol_MenuBarBg] = vec4(p.panel);
  c[ImGuiCol_ScrollbarBg] = vec4(with_alpha(p.void_, 0.0f));
  c[ImGuiCol_ScrollbarGrab] = vec4(p.panel_border_solid);
  c[ImGuiCol_ScrollbarGrabHovered] = vec4(p.ink_muted);
  c[ImGuiCol_ScrollbarGrabActive] = vec4(p.ink_soft);
  c[ImGuiCol_CheckMark] = vec4(p.accent);
  c[ImGuiCol_SliderGrab] = vec4(p.accent);
  c[ImGuiCol_SliderGrabActive] = vec4(p.accent_hover);
  c[ImGuiCol_Button] = vec4(p.card);
  c[ImGuiCol_ButtonHovered] = vec4(p.card_hover);
  c[ImGuiCol_ButtonActive] = vec4(p.card_selected);
  c[ImGuiCol_Header] = vec4(p.card);
  c[ImGuiCol_HeaderHovered] = vec4(p.card_hover);
  c[ImGuiCol_HeaderActive] = vec4(p.card_selected);
  c[ImGuiCol_Separator] = vec4(p.panel_border);
  c[ImGuiCol_SeparatorHovered] = vec4(p.accent);
  c[ImGuiCol_SeparatorActive] = vec4(p.accent);
  c[ImGuiCol_ResizeGrip] = vec4(with_alpha(p.ink_soft, 0.0f));
  c[ImGuiCol_ResizeGripHovered] = vec4(p.card_hover);
  c[ImGuiCol_ResizeGripActive] = vec4(p.card_selected);
  c[ImGuiCol_Tab] = vec4(p.card);
  c[ImGuiCol_TabHovered] = vec4(p.card_hover);
  c[ImGuiCol_TabActive] = vec4(p.accent_surface);
  c[ImGuiCol_TabUnfocused] = vec4(p.card);
  c[ImGuiCol_TabUnfocusedActive] = vec4(p.card);
  c[ImGuiCol_PlotLines] = vec4(p.ink_soft);
  c[ImGuiCol_PlotLinesHovered] = vec4(p.accent);
  c[ImGuiCol_PlotHistogram] = vec4(p.ink_soft);
  c[ImGuiCol_PlotHistogramHovered] = vec4(p.accent);
  c[ImGuiCol_TableHeaderBg] = vec4(p.panel);
  c[ImGuiCol_TableBorderStrong] = vec4(p.panel_border_solid);
  c[ImGuiCol_TableBorderLight] = vec4(p.panel_border);
  c[ImGuiCol_TableRowBg] = vec4(with_alpha(p.void_, 0.0f));
  c[ImGuiCol_TableRowBgAlt] = vec4(with_alpha(p.ink_soft, 0.03f));
  c[ImGuiCol_TextLink] = vec4(p.accent);
  c[ImGuiCol_NavCursor] = vec4(p.accent);
  c[ImGuiCol_NavWindowingHighlight] = vec4(p.accent);
  c[ImGuiCol_NavWindowingDimBg] = vec4(with_alpha(p.void_, 0.20f));
  c[ImGuiCol_ModalWindowDimBg] = vec4(with_alpha(p.void_, 0.60f));
  c[ImGuiCol_DragDropTarget] = vec4(p.accent);
}

const Metrics& metrics() {
  static const Metrics m = {
      48.0f, // navbar_height
      68.0f, // rail_width
      72.0f, // header_height
      40.0f, // statusbar_height
      32.0f, // content_x (clamp(24px, 3vw, 48px) at 1280+)
      4.0f,  // radius_sm
      5.0f,  // radius_md
      10.0f, // radius_lg
      5.0f,  // scrollbar_size
  };
  return m;
}

static TypeScale g_type_scale;

void set_type_scale(const TypeScale& scale) { g_type_scale = scale; }

const TypeScale& type_scale() { return g_type_scale; }

void draw_panel(ImDrawList* list, const ImVec2& min, const ImVec2& max, const Rgb& fill,
                const Rgb& border, float chamfer, float rounding, float border_thickness) {
  if (chamfer <= 0.5f) {
    list->AddRectFilled(min, max, col(fill), rounding);
    if (border.a > 0.001f && border_thickness > 0.0f) {
      list->AddRect(min, max, col(border), rounding, 0, border_thickness);
    }
    return;
  }
  // Top-left and bottom-right corners are clipped — the deck's chamfer.
  const ImVec2 points[6] = {
      ImVec2(min.x + chamfer, min.y),
      max,
      ImVec2(min.x, max.y - chamfer),
  };
  list->AddConvexPolyFilled(points, 3, col(fill));
  if (border.a > 0.001f && border_thickness > 0.0f) {
    list->AddPolyline(points, 3, col(border), ImDrawFlags_Closed, border_thickness);
  }
}

void draw_shadow(ImDrawList* list, const ImVec2& min, const ImVec2& max, float rounding) {
  const Palette& p = active_palette();
  // 6px 5px 12px 3px rgba(29,28,28,0.21), faked with three offset rects.
  const Rgb shadow = p.shadow;
  for (int i = 3; i >= 1; --i) {
    const float t = static_cast<float>(i) / 3.0f;
    Rgb layer = shadow;
    layer.a *= 0.35f * (1.0f - t * 0.5f);
    const float dx = 2.0f * t;
    const float dy = 1.6f * t;
    const float spread = 4.0f * t;
    list->AddRectFilled(ImVec2(min.x + dx - spread, min.y + dy - spread * 0.5f),
                        ImVec2(max.x + dx + spread, max.y + dy + spread * 0.5f), col(layer),
                        rounding + spread * 0.5f);
  }
}

void draw_glow(ImDrawList* list, const ImVec2& center, float radius, const Rgb& color,
               float strength) {
  if (radius <= 0.0f) return;
  const int steps = 10;
  for (int i = steps; i >= 1; --i) {
    const float t = static_cast<float>(i) / static_cast<float>(steps);
    Rgb c = color;
    c.a = color.a * strength * (1.0f - t);
    list->AddCircleFilled(center, radius * t, col(c), 24);
  }
}

void draw_gradient(ImDrawList* list, const ImVec2& min, const ImVec2& max, const Rgb& left,
                   const Rgb& right) {
  const ImU32 left_col = col(left);
  const ImU32 right_col = col(right);
  list->PushClipRect(min, max, true);
  const float width = max.x - min.x;
  const int steps = 24;
  for (int i = 0; i < steps; ++i) {
    const float t0 = static_cast<float>(i) / static_cast<float>(steps);
    const float t1 = static_cast<float>(i + 1) / static_cast<float>(steps);
    const float x0 = min.x + width * t0;
    const float x1 = min.x + width * t1 + 1.0f;
    const ImU32 c = ImGui::ColorConvertFloat4ToU32(ImVec4(left.r + (right.r - left.r) * ((t0 + t1) * 0.5f),
                 left.g + (right.g - left.g) * ((t0 + t1) * 0.5f),
                 left.b + (right.b - left.b) * ((t0 + t1) * 0.5f),
                 left.a + (right.a - left.a) * ((t0 + t1) * 0.5f)));
    list->AddRectFilled(ImVec2(x0, min.y), ImVec2(x1, max.y), c);
    (void)left_col;
    (void)right_col;
  }
  list->PopClipRect();
}

void text(const char* s, const Rgb& color, ImFont* font) {
  if (!s || !*s) return;
  if (font) ImGui::PushFont(font);
  ImGui::TextColored(vec4(color), "%s", s);
  if (font) ImGui::PopFont();
}

void text_at(ImDrawList* list, const ImVec2& pos, const Rgb& color, const char* s,
             ImFont* font) {
  if (!s || !*s) return;
  if (font) {
    // AddText's font overload binds the atlas texture itself, so no manual
    // PushTextureID is needed.
    list->AddText(font, font->FontSize, pos, col(color), s);
    return;
  }
  list->AddText(pos, col(color), s);
}

ImVec2 measure_text(const char* s, ImFont* font) {
  if (!s) return ImVec2(0.0f, 0.0f);
  if (font) {
    ImGui::PushFont(font);
    const ImVec2 out = ImGui::CalcTextSize(s);
    ImGui::PopFont();
    return out;
  }
  return ImGui::CalcTextSize(s);
}

std::string ellipsize(const std::string& text, float max_width, ImFont* font) {
  if (text.empty()) return text;
  if (measure_text(text.c_str(), font).x <= max_width) return text;
  const std::string ellipsis = "...";
  size_t end = text.size();
  while (end > 0) {
    const std::string candidate = text.substr(0, end) + ellipsis;
    if (measure_text(candidate.c_str(), font).x <= max_width) return candidate;
    do {
      --end;
    } while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80);
  }
  return ellipsis;
}

} // namespace onow
