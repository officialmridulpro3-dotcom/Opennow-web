// The OpenNOW widget set.
//
// ImGui gives us buttons, sliders and inputs. What it does not give us is the
// deck's own vocabulary: chamfered panels, neon underlines, poster cards with
// a hover scrim, a HUD with tracked-out digits, or the stream deck's pill
// buttons. Those live here so every view composes the same primitives and the
// styling stays in exactly one place — the same rule the web client enforced
// with its `.sv--*` / `.qs--*` CSS namespaces.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

#include "onow/ui/Theme.h"

namespace onow {

struct WidgetContext {
  float scale = 1.0f;   // DPI scale
  float dt = 0.0f;      // seconds since last frame
  float time = 0.0f;    // seconds since start (for ambient loops)
};

// ------------------------------------------------------------------ layout --

// Full-viewport host window with no chrome. Every view draws inside one of
// these, which is what makes the app look like an app and not a demo.
bool begin_surface(const char* id, ImGuiWindowFlags flags = 0);
void end_surface();

// The app frame: rail + header + content + status bar. Returns the content
// rect the page should draw into.
struct ShellLayout {
  ImVec2 rail_min;
  ImVec2 rail_max;
  ImVec2 header_min;
  ImVec2 header_max;
  ImVec2 content_min;
  ImVec2 content_max;
  ImVec2 status_min;
  ImVec2 status_max;
};
ShellLayout shell_layout(const ImVec2& viewport_min, const ImVec2& viewport_max,
                         bool show_rail = true, bool show_header = true,
                         bool show_status = true);

// ---------------------------------------------------------------- controls --

struct ButtonStyle {
  Rgb background = Rgb();
  Rgb background_hovered = Rgb();
  Rgb background_active = Rgb();
  Rgb text = Rgb();
  Rgb border = Rgb();
  float border_thickness = 1.0f;
  float rounding = 5.0f;
  float chamfer = 0.0f;
  bool glow = false;
  ImFont* font = nullptr;
};

// The primary "chamfered neon" button.
bool button(const std::string& label, const ImVec2& size = ImVec2(0, 0),
            const ButtonStyle& style = ButtonStyle());
bool button_accent(const std::string& label, const ImVec2& size = ImVec2(0, 0));
bool button_ghost(const std::string& label, const ImVec2& size = ImVec2(0, 0));
bool button_danger(const std::string& label, const ImVec2& size = ImVec2(0, 0));

// Icon-only square button. `glyph` is a single UTF-8 codepoint (e.g. "▶").
bool icon_button(const std::string& glyph, const ImVec2& size = ImVec2(32, 32),
                 const std::string& tooltip = "");

// Small pill used for filters, tags and status chips.
bool chip(const std::string& label, bool selected = false, const ImVec2& size = ImVec2(0, 0));

// Section heading with the deck's accent underline.
void section_header(const std::string& title, const std::string& subtitle = "");

// A labelled settings row: label on the left, control on the right.
bool setting_row(const std::string& label, const std::string& description = "");
void setting_row_end();

// Sliders/inputs that follow the palette. `id` must be unique per row.
bool slider_float(const std::string& label, float* value, float min, float max,
                  const char* format = "%.2f");
bool slider_int(const std::string& label, int* value, int min, int max,
                 const char* format = "%d");
bool toggle(const std::string& label, bool* value);
bool combo(const std::string& label, const std::vector<std::string>& items, int* index);
bool input_text(const std::string& label, std::string* value, const char* hint = "");
bool input_key(const std::string& label, std::string* value);

// ------------------------------------------------------------------ cards ---

struct PosterCard {
  std::string title;
  std::string subtitle;
  std::string store;
  ImTextureID cover = {}; // 0 = placeholder gradient
  bool favorite = false;
  bool selected = false;
  bool active_session = false;
  bool in_library = true;
  float width = 0.0f; // 0 = derive from the poster aspect
  float aspect = 0.75f;
};

// Draws one poster card at the cursor. Returns true when activated.
bool poster_card(const PosterCard& card);

// Horizontal scroll strip of cards (the home page's shelves).
void card_row(const std::string& id, const std::vector<PosterCard>& cards, float height,
              const std::function<void(size_t)>& on_select,
              const std::function<void(size_t)>& on_activate, size_t* selected);

// The big hero used on Home and on the stream loading screen.
void hero(const std::string& title, const std::string& subtitle, ImTextureID backdrop,
          const std::string& primary_label, const std::string& secondary_label,
          const std::function<void()>& on_primary, const std::function<void()>& on_secondary);

// ------------------------------------------------------------------- HUD ----

// Tracked-out monospace line used by the stats HUD.
void hud_line(const std::string& label, const std::string& value, const Rgb& value_color);

void hud_panel(const std::string& title, const std::function<void()>& body);

// --------------------------------------------------------------- feedback --

enum class ToastKind { Info, Success, Warning, Error };
void toast(ToastKind kind, const std::string& title, const std::string& detail = "");
// Drains the queued toasts into the top-right stack. Call once per frame.
void draw_toasts();

// A modal dialog with the deck's chamfered card. Returns false when dismissed.
bool modal(const std::string& id, const std::string& title, const std::string& body,
           const std::string& confirm_label, const std::string& cancel_label,
           const std::function<void()>& on_confirm, bool* open);

// Spinner + label for the loading states (queue / setup / connecting).
void spinner(const char* label, float radius = 14.0f);

// Progress bar with the accent fill.
void progress(float fraction, const std::string& label = "");

// Thin scrollbar styling helper — the CSS uses 5px bars.
void begin_scroll_child(const char* id, const ImVec2& size);
void end_scroll_child();

// Wraps text with the palette's body font and returns the height used.
float wrapped_text(const std::string& text, float width, const Rgb& color);

} // namespace onow
