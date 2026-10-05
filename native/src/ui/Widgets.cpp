#include "onow/ui/Widgets.h"

#include "onow/Str.h"

#include <cmath>
#include <cstdio>

namespace onow {

namespace {

WidgetContext g_ctx;
std::vector<std::pair<ToastKind, std::pair<std::string, std::string>>> g_toasts;
int g_toast_id = 0;

Rgb toast_color(ToastKind kind) {
  const Palette& p = active_palette();
  switch (kind) {
    case ToastKind::Info: return p.info;
    case ToastKind::Success: return p.success;
    case ToastKind::Warning: return p.warning;
    case ToastKind::Error: return p.error;
  }
  return p.info;
}

void styled_button(const std::string& label, const ImVec2& size, const ButtonStyle& style) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, style.rounding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, style.border_thickness);
  ImGui::PushStyleColor(ImGuiCol_Button, col(style.background));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(style.background_hovered));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(style.background_active));
  ImGui::PushStyleColor(ImGuiCol_Border, col(style.border));
  ImGui::PushStyleColor(ImGuiCol_Text, col(style.text));
  if (style.font) ImGui::PushFont(style.font);
  ImGui::Button(label.c_str(), size);
  if (style.font) ImGui::PopFont();
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
}

} // namespace

bool begin_surface(const char* id, ImGuiWindowFlags flags) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(active_palette().bg_a));
  const bool open = ImGui::Begin(id, nullptr,
                                 flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNavFocus);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  return open;
}

void end_surface() { ImGui::End(); }

ShellLayout shell_layout(const ImVec2& viewport_min, const ImVec2& viewport_max, bool show_rail,
                         bool show_header, bool show_status) {
  const Metrics& m = metrics();
  const float scale = g_ctx.scale;
  ShellLayout layout;
  const float rail_w = show_rail ? m.rail_width * scale : 0.0f;
  const float header_h = show_header ? m.header_height * scale : 0.0f;
  const float status_h = show_status ? m.statusbar_height * scale : 0.0f;

  layout.rail_min = viewport_min;
  layout.rail_max = ImVec2(viewport_min.x + rail_w, viewport_max.y);
  layout.header_min = ImVec2(viewport_min.x + rail_w, viewport_min.y);
  layout.header_max = ImVec2(viewport_max.x, viewport_min.y + header_h);
  layout.content_min = ImVec2(viewport_min.x + rail_w, viewport_min.y + header_h);
  layout.content_max = ImVec2(viewport_max.x, viewport_max.y - status_h);
  layout.status_min = ImVec2(viewport_min.x, viewport_max.y - status_h);
  layout.status_max = viewport_max;
  return layout;
}

bool button(const std::string& label, const ImVec2& size, const ButtonStyle& style) {
  const Palette& p = active_palette();
  ButtonStyle s = style;
  if (s.background.a == 0.0f && s.background.r == 0.0f && s.background.g == 0.0f &&
      s.background.b == 0.0f && s.background_hovered.a == 0.0f) {
    // Unstyled call: use the card surface.
    s.background = p.card;
    s.background_hovered = p.card_hover;
    s.background_active = p.card_selected;
    s.text = p.ink;
    s.border = p.panel_border;
  }
  styled_button(label, size, s);
  return ImGui::IsItemActivated() || ImGui::IsItemDeactivatedAfterEdit();
}

bool button_accent(const std::string& label, const ImVec2& size) {
  const Palette& p = active_palette();
  ButtonStyle s;
  s.background = p.accent;
  s.background_hovered = p.accent_hover;
  s.background_active = p.accent_press;
  s.text = p.accent_on;
  s.border = p.accent;
  s.chamfer = 8.0f * g_ctx.scale;
  s.glow = true;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const bool pressed = button(label, size, s);
  if (s.glow && ImGui::IsItemHovered()) {
    draw_glow(ImGui::GetWindowDrawList(), ImVec2(cursor.x + ImGui::GetItemRectSize().x * 0.5f,
                                                 cursor.y + ImGui::GetItemRectSize().y * 0.5f),
              ImGui::GetItemRectSize().x * 0.6f, p.accent_glow, 0.35f);
  }
  return pressed;
}

bool button_ghost(const std::string& label, const ImVec2& size) {
  const Palette& p = active_palette();
  ButtonStyle s;
  s.background = Rgb();
  s.background_hovered = p.card;
  s.background_active = p.card_hover;
  s.text = p.ink_soft;
  s.border = p.panel_border;
  return button(label, size, s);
}

bool button_danger(const std::string& label, const ImVec2& size) {
  const Palette& p = active_palette();
  ButtonStyle s;
  s.background = p.error_bg;
  s.background_hovered = mix(p.error_bg, p.error, 0.25f);
  s.background_active = mix(p.error_bg, p.error, 0.45f);
  s.text = p.error;
  s.border = p.error_border;
  return button(label, size, s);
}

bool icon_button(const std::string& glyph, const ImVec2& size, const std::string& tooltip) {
  const Palette& p = active_palette();
  ButtonStyle s;
  s.background = p.card;
  s.background_hovered = p.card_hover;
  s.background_active = p.card_selected;
  s.text = p.ink_soft;
  s.border = p.panel_border;
  const bool pressed = button(glyph, size, s);
  if (!tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip.c_str());
  return pressed;
}

bool chip(const std::string& label, bool selected, const ImVec2& size) {
  const Palette& p = active_palette();
  ButtonStyle s;
  if (selected) {
    s.background = p.card_selected;
    s.background_hovered = mix(p.card_selected, p.accent, 0.2f);
    s.background_active = mix(p.card_selected, p.accent, 0.35f);
    s.text = p.accent;
    s.border = mix(p.accent, p.void_, 0.5f);
  } else {
    s.background = p.chip;
    s.background_hovered = p.card_hover;
    s.background_active = p.card_selected;
    s.text = p.ink_soft;
    s.border = Rgb();
  }
  s.rounding = 999.0f;
  return button(label, size, s);
}

void section_header(const std::string& title, const std::string& subtitle) {
  const Palette& p = active_palette();
  ImGui::Dummy(ImVec2(0.0f, 6.0f * g_ctx.scale));
  ImGui::PushFont(type_scale().title);
  ImGui::TextColored(vec4(p.ink), "%s", title.c_str());
  ImGui::PopFont();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->AddLine(ImVec2(cursor.x, cursor.y - 2.0f), ImVec2(cursor.x + 44.0f * g_ctx.scale, cursor.y - 2.0f),
                col(p.accent), 2.0f * g_ctx.scale);
  if (!subtitle.empty()) {
    ImGui::PushFont(type_scale().caption);
    ImGui::TextColored(vec4(p.ink_muted), "%s", subtitle.c_str());
    ImGui::PopFont();
  }
  (void)width;
  ImGui::Dummy(ImVec2(0.0f, 4.0f * g_ctx.scale));
}

bool setting_row(const std::string& label, const std::string& description) {
  const float scale = g_ctx.scale;
  const float width = ImGui::GetContentRegionAvail().x;
  const float label_width = width * 0.42f;
  ImGui::PushID(label.c_str());
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(vec4(active_palette().ink), "%s", ellipsize(label, label_width, type_scale().body).c_str());
  if (!description.empty() && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", description.c_str());
  }
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - label_width - 220.0f * scale));
  ImGui::PushItemWidth(220.0f * scale);
  return true;
}

void setting_row_end() {
  ImGui::PopItemWidth();
  ImGui::PopID();
}

bool slider_float(const std::string& label, float* value, float min, float max, const char* format) {
  setting_row(label);
  const bool changed = ImGui::SliderFloat("##v", value, min, max, format);
  setting_row_end();
  return changed;
}

bool slider_int(const std::string& label, int* value, int min, int max, const char* format) {
  setting_row(label);
  const bool changed = ImGui::SliderInt("##v", value, min, max, format);
  setting_row_end();
  return changed;
}

bool toggle(const std::string& label, bool* value) {
  setting_row(label);
  const bool changed = ImGui::Checkbox("##v", value);
  setting_row_end();
  return changed;
}

bool combo(const std::string& label, const std::vector<std::string>& items, int* index) {
  setting_row(label);
  const char* preview = (*index >= 0 && *index < static_cast<int>(items.size()))
                            ? items[static_cast<size_t>(*index)].c_str()
                            : "";
  bool changed = false;
  if (ImGui::BeginCombo("##v", preview)) {
    for (size_t i = 0; i < items.size(); ++i) {
      const bool selected = static_cast<int>(i) == *index;
      if (ImGui::Selectable(items[i].c_str(), selected) && !selected) {
        *index = static_cast<int>(i);
        changed = true;
      }
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  setting_row_end();
  return changed;
}

bool input_text(const std::string& label, std::string* value, const char* hint) {
  setting_row(label);
  char buffer[512];
  std::snprintf(buffer, sizeof(buffer), "%s", value->c_str());
  const bool changed = ImGui::InputTextWithHint("##v", hint ? hint : "", buffer, sizeof(buffer));
  if (changed) *value = buffer;
  setting_row_end();
  return changed;
}

bool input_key(const std::string& label, std::string* value) {
  setting_row(label);
  const Palette& p = active_palette();
  ImGui::PushStyleColor(ImGuiCol_Text, col(p.accent));
  const bool changed = ImGui::InputText("##v", value->data(), value->capacity() + 1,
                                        ImGuiInputTextFlags_ReadOnly);
  ImGui::PopStyleColor();
  setting_row_end();
  return changed;
}

bool poster_card(const PosterCard& card) {
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const float width = card.width > 0.0f ? card.width : 180.0f * scale;
  const float height = width / (card.aspect > 0.0f ? card.aspect : 0.75f);

  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, height);
  ImDrawList* list = ImGui::GetWindowDrawList();

  const bool selected = card.selected;
  const bool hovered = ImGui::IsMouseHoveringRect(cursor, ImVec2(cursor.x + width, cursor.y + height));

  // Card surface. The CSS uses a 8% steel fill that goes to 14% on hover and
  // to the accent's 12% when selected.
  Rgb fill = selected ? p.card_selected : (hovered ? p.card_hover : p.card);
  list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), col(fill),
                      metrics().radius_md * scale);
  if (card.cover) {
    // The artwork fills the card and the scrim sits on top.
    list->AddImageRounded(card.cover, cursor, ImVec2(cursor.x + width, cursor.y + height), ImVec2(0, 0),
                          ImVec2(1, 1), IM_COL32_WHITE, metrics().radius_md * scale);
    if (hovered || selected) {
      list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height),
                          col(mix(p.void_, p.accent, selected ? 0.16f : 0.08f)),
                          metrics().radius_md * scale);
    }
  } else {
    // Placeholder: the layered blue-black gradient with the title in the middle.
    draw_gradient(list, cursor, ImVec2(cursor.x + width, cursor.y + height), p.bg_b, p.bg_c);
    text_at(list, ImVec2(cursor.x + 10.0f * scale, cursor.y + height * 0.5f - 8.0f * scale),
            p.ink_muted, ellipsize(card.title, width - 20.0f * scale, type_scale().caption).c_str(),
            type_scale().caption);
  }
  if (selected) {
    list->AddRect(cursor, ImVec2(cursor.x + width, cursor.y + height), col(p.accent),
                  metrics().radius_md * scale, 0, 2.0f * scale);
  }

  // Title strip under the artwork.
  const float strip_h = 22.0f * scale;
  list->AddRectFilled(ImVec2(cursor.x, cursor.y + height - strip_h),
                      ImVec2(cursor.x + width, cursor.y + height),
                      col(mix(p.void_, p.void_, 0.0f)), metrics().radius_md * scale,
                      ImDrawFlags_RoundCornersBottom);
  text_at(list, ImVec2(cursor.x + 8.0f * scale, cursor.y + height - strip_h + 3.0f * scale), p.ink,
          ellipsize(card.title, width - 16.0f * scale, type_scale().caption).c_str(),
          type_scale().caption);

  if (card.favorite) {
    text_at(list, ImVec2(cursor.x + width - 20.0f * scale, cursor.y + 6.0f * scale), p.accent, "*",
            type_scale().caption);
  }

  ImGui::InvisibleButton("##card", size);
  const bool activated = ImGui::IsItemActivated();
  if (ImGui::IsItemHovered() && !card.subtitle.empty()) {
    ImGui::SetTooltip("%s", card.subtitle.c_str());
  }
  return activated;
}

void card_row(const std::string& id, const std::vector<PosterCard>& cards, float height,
              const std::function<void(size_t)>& on_select,
              const std::function<void(size_t)>& on_activate, size_t* selected) {
  ImGui::PushID(id.c_str());
  ImGui::BeginChild("row", ImVec2(0.0f, height), false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  float x = 0.0f;
  for (size_t i = 0; i < cards.size(); ++i) {
    ImGui::SetCursorPosX(x);
    ImGui::BeginGroup();
    PosterCard card = cards[i];
    card.width = height * (card.aspect > 0.0f ? card.aspect : 0.75f);
    const bool activated = poster_card(card);
    if (activated && on_activate) on_activate(i);
    if (ImGui::IsItemHovered() && on_select) on_select(i);
    if (selected && *selected == i) {
      // Selection is expressed through the card itself.
    }
    ImGui::EndGroup();
    x += card.width + 12.0f * g_ctx.scale;
  }
  ImGui::EndChild();
  ImGui::PopID();
}

void hero(const std::string& title, const std::string& subtitle, ImTextureID backdrop,
          const std::string& primary_label, const std::string& secondary_label,
          const std::function<void()>& on_primary, const std::function<void()>& on_secondary) {
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 260.0f * scale;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImDrawList* list = ImGui::GetWindowDrawList();

  draw_gradient(list, cursor, ImVec2(cursor.x + width, cursor.y + height), p.bg_b, p.bg_c);
  if (backdrop) {
    list->AddImageRounded(backdrop, cursor, ImVec2(cursor.x + width, cursor.y + height), ImVec2(0, 0),
                          ImVec2(1, 1), IM_COL32_WHITE, metrics().radius_lg * scale);
    // Left-to-right scrim so the text always reads.
    for (int i = 0; i < 24; ++i) {
      const float t = static_cast<float>(i) / 24.0f;
      Rgb scrim = p.bg_a;
      scrim.a = 0.85f * (1.0f - t);
      list->AddRectFilled(ImVec2(cursor.x + width * t, cursor.y),
                          ImVec2(cursor.x + width * (t + 1.0f / 24.0f) + 1.0f, cursor.y + height),
                          col(scrim));
    }
  }

  ImGui::PushFont(type_scale().display);
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 28.0f * scale,
                             ImGui::GetCursorPosY() + 56.0f * scale));
  ImGui::TextColored(vec4(p.ink), "%s", title.c_str());
  ImGui::PopFont();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 28.0f * scale);
  ImGui::PushFont(type_scale().body);
  ImGui::TextColored(vec4(p.ink_soft), "%s", subtitle.c_str());
  ImGui::PopFont();
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 28.0f * scale,
                             ImGui::GetCursorPosY() + 18.0f * scale));
  if (!primary_label.empty() && on_primary) {
    if (button_accent(primary_label, ImVec2(180.0f * scale, 40.0f * scale))) on_primary();
    ImGui::SameLine();
  }
  if (!secondary_label.empty() && on_secondary) {
    if (button_ghost(secondary_label, ImVec2(150.0f * scale, 40.0f * scale))) on_secondary();
  }
  ImGui::Dummy(ImVec2(0.0f, height));
}

void hud_line(const std::string& label, const std::string& value, const Rgb& value_color) {
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  ImGui::PushFont(type_scale().mono);
  const float label_width = 92.0f * scale;
  ImGui::TextColored(vec4(p.ink_muted), "%s", label.c_str());
  ImGui::SameLine(0.0f, 12.0f * scale);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() - ImGui::CalcTextSize(label.c_str()).x + label_width);
  ImGui::TextColored(vec4(value_color), "%s", value.c_str());
  ImGui::PopFont();
}

void hud_panel(const std::string& title, const std::function<void()>& body) {
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 210.0f * scale;
  ImDrawList* list = ImGui::GetWindowDrawList();
  draw_shadow(list, cursor, ImVec2(cursor.x + width, cursor.y + height), metrics().radius_lg * scale);
  draw_panel(list, cursor, ImVec2(cursor.x + width, cursor.y + height), mix(p.void_, p.void_, 0.86f),
             p.panel_border, 0.0f, metrics().radius_lg * scale, 1.0f);
  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
  ImGui::Indent(14.0f * scale);
  ImGui::PushFont(type_scale().caption);
  ImGui::TextColored(vec4(p.ink_muted), "%s", title.c_str());
  ImGui::PopFont();
  if (body) body();
  ImGui::Unindent(14.0f * scale);
  ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
}

void toast(ToastKind kind, const std::string& title, const std::string& detail) {
  g_toasts.push_back({kind, {title, detail}});
}

void draw_toasts() {
  if (g_toasts.empty()) return;
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImDrawList* list = ImGui::GetForegroundDrawList();
  float y = viewport->WorkPos.y + 80.0f * scale;
  const float width = 340.0f * scale;
  for (const auto& entry : g_toasts) {
    const Rgb color = toast_color(entry.first);
    const ImVec2 min(viewport->WorkPos.x + viewport->WorkSize.x - width - 24.0f * scale, y);
    const ImVec2 max(viewport->WorkPos.x + viewport->WorkSize.x - 24.0f * scale, y + 56.0f * scale);
    draw_shadow(list, min, max, metrics().radius_md * scale);
    draw_panel(list, min, max, mix(p.panel, color, 0.12f), p.panel_border, 0.0f,
               metrics().radius_md * scale, 1.0f);
    list->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + 3.0f * scale, max.y), col(color));
    text_at(list, ImVec2(min.x + 14.0f * scale, min.y + 10.0f * scale), p.ink,
            entry.second.first.c_str(), type_scale().body);
    if (!entry.second.second.empty()) {
      text_at(list, ImVec2(min.x + 14.0f * scale, min.y + 30.0f * scale), p.ink_muted,
              ellipsize(entry.second.second, width - 28.0f * scale, type_scale().caption).c_str(),
              type_scale().caption);
    }
    y += 64.0f * scale;
  }
  // Toasts live for one frame of queueing plus the frame they are drawn in.
  g_toasts.clear();
}

bool modal(const std::string& id, const std::string& title, const std::string& body,
           const std::string& confirm_label, const std::string& cancel_label,
           const std::function<void()>& on_confirm, bool* open) {
  if (!*open) return false;
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 size(460.0f * scale, 0.0f);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x - size.x * 0.5f, viewport->GetCenter().y - 110.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * scale, 20.0f * scale));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(p.panel));
  ImGui::PushStyleColor(ImGuiCol_Border, col(p.panel_border));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics().radius_lg * scale);
  const bool visible = ImGui::Begin(id.c_str(), nullptr,
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_Modal);
  if (visible) {
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", title.c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    wrapped_text(body, ImGui::GetContentRegionAvail().x, p.ink_soft);
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    const float button_width = 150.0f * scale;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - button_width * 2.0f - 8.0f * scale);
    if (!cancel_label.empty()) {
      if (button_ghost(cancel_label, ImVec2(button_width, 36.0f * scale))) *open = false;
      ImGui::SameLine();
    }
    if (button_accent(confirm_label, ImVec2(button_width, 36.0f * scale))) {
      if (on_confirm) on_confirm();
      *open = false;
    }
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
  return visible;
}

void spinner(const char* label, float radius) {
  const Palette& p = active_palette();
  const float scale = g_ctx.scale;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const float r = radius * scale;
  ImDrawList* list = ImGui::GetWindowDrawList();
  const float t = g_ctx.time * 2.4f;
  for (int i = 0; i < 12; ++i) {
    const float angle = t + static_cast<float>(i) * (6.2831853f / 12.0f);
    const float fade = 1.0f - static_cast<float>(i) / 12.0f;
    Rgb c = p.accent;
    c.a = fade * 0.9f;
    list->AddCircleFilled(ImVec2(cursor.x + r + std::cos(angle) * r, cursor.y + r + std::sin(angle) * r),
                          2.2f * scale, col(c), 12);
  }
  if (label && *label) {
    ImGui::Dummy(ImVec2(r * 2.0f + 12.0f * scale, r * 2.0f));
    ImGui::SameLine();
    ImGui::PushFont(type_scale().body);
    ImGui::TextColored(vec4(p.ink_soft), "%s", label);
    ImGui::PopFont();
  } else {
    ImGui::Dummy(ImVec2(r * 2.0f, r * 2.0f));
  }
}

void progress(float fraction, const std::string& label) {
  const Palette& p = active_palette();
  if (fraction < 0.0f) fraction = 0.0f;
  if (fraction > 1.0f) fraction = 1.0f;
  const float scale = g_ctx.scale;
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = 6.0f * scale;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->AddRectFilled(cursor, ImVec2(cursor.x + width, cursor.y + height), col(p.card), height * 0.5f);
  if (fraction > 0.0f) {
    list->AddRectFilled(cursor, ImVec2(cursor.x + width * fraction, cursor.y + height), col(p.accent),
                        height * 0.5f);
  }
  if (!label.empty()) {
    ImGui::Dummy(ImVec2(width, height + 4.0f * scale));
    ImGui::PushFont(type_scale().caption);
    ImGui::TextColored(vec4(p.ink_muted), "%s", label.c_str());
    ImGui::PopFont();
  } else {
    ImGui::Dummy(ImVec2(width, height));
  }
}

void begin_scroll_child(const char* id, const ImVec2& size) {
  const Palette& p = active_palette();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, col(mix(p.bg_a, p.void_, 0.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, metrics().radius_lg * g_ctx.scale);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, metrics().scrollbar_size * g_ctx.scale);
  ImGui::BeginChild(id, size, false,
                    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);
}

void end_scroll_child() {
  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

float wrapped_text(const std::string& text, float width, const Rgb& color) {
  const Palette& p = active_palette();
  (void)p;
  ImGui::PushFont(type_scale().body);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  ImGui::TextColored(vec4(color), "%s", text.c_str());
  const float height = ImGui::GetItemRectSize().y;
  ImGui::PopTextWrapPos();
  ImGui::PopFont();
  return height;
}

} // namespace onow
