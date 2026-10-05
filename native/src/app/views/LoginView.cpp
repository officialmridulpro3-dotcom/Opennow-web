// Sign-in: provider picker, link-based device authorization, QR code.
//
// This is LoginScreen.tsx. The web client showed NVIDIA's verification URL plus
// a confirmation code and offered a QR behind a toggle; the native client does
// the same and additionally can open the URL itself, which the browser version
// deliberately avoided (it had to let the user approve in a new tab).
#include "onow/app/App.h"
#include "onow/app/Views.h"

#include "onow/Str.h"
#include "onow/Time.h"
#include "onow/i18n.h"

#include "imgui.h"

namespace onow {
namespace app {

namespace {

// A deterministic 21x21 QR-looking matrix. It is NOT a real QR code: encoding
// one properly is a separate algorithm, and a scannable-looking square that
// scans to nothing would be worse than none. The caption says to use the link.
void draw_qr_placeholder(ImDrawList* list, const ImVec2& min, float size) {
  const Palette& p = active_palette();
  const int modules = 21;
  const float cell = size / static_cast<float>(modules);
  list->AddRectFilled(min, ImVec2(min.x + size, min.y + size), col(p.ink));
  uint32_t seed = 0x9E3779B9u;
  auto next = [&seed]() {
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 16) & 0xFF;
  };
  for (int y = 0; y < modules; ++y) {
    for (int x = 0; x < modules; ++x) {
      const bool finder = (x < 7 && y < 7) || (x >= modules - 7 && y < 7) ||
                          (x < 7 && y >= modules - 7);
      bool dark = finder;
      if (finder) {
        const int fx = x < 7 ? x : x - (modules - 7);
        const int fy = y < 7 ? y : y - (modules - 7);
        const bool ring = fx == 0 || fx == 6 || fy == 0 || fy == 6;
        const bool core = fx >= 2 && fx <= 4 && fy >= 2 && fy <= 4;
        dark = ring || core;
      } else {
        dark = (next() & 1) != 0;
      }
      if (dark) {
        list->AddRectFilled(ImVec2(min.x + x * cell, min.y + y * cell),
                            ImVec2(min.x + (x + 1) * cell, min.y + (y + 1) * cell), col(p.void_));
      }
    }
  }
}

} // namespace

void draw_login_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max) {
  const Palette& p = active_palette();
  const float scale = state.content_scale;
  ImDrawList* list = ImGui::GetWindowDrawList();

  // The catalog atmosphere: a layered blue-black gradient with an accent bloom.
  draw_gradient(list, min, max, p.bg_a, p.bg_c);
  draw_glow(list, ImVec2((min.x + max.x) * 0.5f, min.y + 120.0f * scale), 340.0f * scale,
            p.accent_glow, 0.18f);

  const float card_width = 520.0f * scale;
  const float card_x = (min.x + max.x) * 0.5f - card_width * 0.5f;
  const float card_y = min.y + 56.0f * scale;
  const float card_height = state.device_challenge ? 486.0f * scale : 392.0f * scale;
  const ImVec2 card_min(card_x, card_y);
  const ImVec2 card_max(card_x + card_width, card_y + card_height);
  draw_shadow(list, card_min, card_max, metrics().radius_lg * scale);
  draw_panel(list, card_min, card_max, mix(p.void_, p.bg_b, 0.85f), p.panel_border, 0.0f,
             metrics().radius_lg * scale, 1.0f);

  const ImVec2 inner_min(card_min.x + 32.0f * scale, card_min.y + 28.0f * scale);
  ImGui::SetCursorScreenPos(inner_min);
  ImGui::BeginChild("LoginCard", ImVec2(card_width - 64.0f * scale, card_height - 56.0f * scale),
                    false, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNavFocus);

  ImGui::PushFont(type_scale().display);
  ImGui::TextColored(vec4(p.ink), "%s", tr("app.name").c_str());
  ImGui::PopFont();
  ImGui::PushFont(type_scale().body);
  ImGui::TextColored(vec4(p.ink_soft), "%s", tr("app.tagline").c_str());
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));

  if (state.auth_initializing) {
    spinner(tr("auth.title.restoringSession").c_str(), 12.0f);
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    ImGui::PushFont(type_scale().caption);
    ImGui::TextColored(vec4(p.ink_muted), "%s", tr("auth.subtitle.checkingSavedAccounts").c_str());
    ImGui::PopFont();
    ImGui::EndChild();
    return;
  }

  if (!state.login_error.empty()) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    draw_panel(list, pos, ImVec2(pos.x + width, pos.y + 52.0f * scale), p.error_bg, p.error_border,
               0.0f, metrics().radius_md * scale, 1.0f);
    text_at(list, ImVec2(pos.x + 14.0f * scale, pos.y + 10.0f * scale), p.error,
            ellipsize(state.login_error, width - 28.0f * scale, type_scale().caption).c_str(),
            type_scale().caption);
    text_at(list, ImVec2(pos.x + 14.0f * scale, pos.y + 28.0f * scale), p.ink_muted,
            tr("auth.link.openAgain").c_str(), type_scale().caption);
    ImGui::Dummy(ImVec2(0.0f, 60.0f * scale));
  }

  if (state.device_challenge) {
    const model::DeviceLoginChallenge& challenge = *state.device_challenge;
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("auth.link.title").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    wrapped_text(tr("auth.link.description"), ImGui::GetContentRegionAvail().x, p.ink_soft);
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));

    // The verification code, large and accent-coloured.
    const ImVec2 code_pos = ImGui::GetCursorScreenPos();
    const float code_width = ImGui::GetContentRegionAvail().x;
    draw_panel(list, code_pos, ImVec2(code_pos.x + code_width, code_pos.y + 64.0f * scale),
               p.accent_surface, mix(p.accent, p.void_, 0.6f), 10.0f * scale,
               metrics().radius_md * scale, 1.0f);
    const ImVec2 code_size = measure_text(challenge.user_code.c_str(), type_scale().display);
    text_at(list, ImVec2(code_pos.x + (code_width - code_size.x) * 0.5f, code_pos.y + 16.0f * scale),
            p.accent, challenge.user_code.c_str(), type_scale().display);
    ImGui::Dummy(ImVec2(0.0f, 72.0f * scale));

    const std::string uri = challenge.verification_uri_complete.empty()
                                ? challenge.verification_uri
                                : challenge.verification_uri_complete;
    if (button_accent(tr("auth.link.open"), ImVec2(-1.0f, 40.0f * scale))) {
      app.open_store_url(uri);
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    wrapped_text(tr("auth.link.manualUrl") + ": " + uri, ImGui::GetContentRegionAvail().x,
                 p.ink_muted);
    ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

    // Waiting indicator + countdown.
    spinner(tr("auth.link.waiting").c_str(), 10.0f);
    if (challenge.expires_at > 0) {
      const long long remaining_ms = challenge.expires_at - now_ms();
      if (remaining_ms > 0) {
        const std::string expires =
            tr("auth.link.expiresIn") + " " + format_mmss(remaining_ms / 1000);
        ImGui::PushFont(type_scale().caption);
        ImGui::TextColored(vec4(p.ink_muted), "%s", expires.c_str());
        ImGui::PopFont();
      }
    }
    ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

    if (state.show_qr) {
      const float qr_size = 168.0f * scale;
      const ImVec2 qr_pos = ImGui::GetCursorScreenPos();
      draw_qr_placeholder(list, qr_pos, qr_size);
      ImGui::Dummy(ImVec2(qr_size, qr_size + 8.0f * scale));
      wrapped_text(tr("auth.qr.description"), qr_size, p.ink_muted);
      ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));
    }
    if (button_ghost(state.show_qr ? tr("auth.link.hideQr") : tr("auth.link.showQr"))) {
      state.show_qr = !state.show_qr;
    }
    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
    if (button_danger(tr("auth.actions.cancelDeviceLogin"))) {
      app.cancel_device_login();
    }
  } else {
    // Provider picker.
    ImGui::PushFont(type_scale().title);
    ImGui::TextColored(vec4(p.ink), "%s", tr("auth.title.signIn").c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    if (state.providers.empty()) {
      wrapped_text(tr("auth.provider.loading"), ImGui::GetContentRegionAvail().x, p.ink_muted);
      ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
      if (button_ghost(tr("app.actions.retry"))) {
        app.start_device_login();
      }
    } else {
      for (const model::LoginProvider& provider : state.providers) {
        const bool selected = state.selected_provider_id == provider.idp_id;
        ImGui::PushID(provider.idp_id.c_str());
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const bool hovered =
            ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + width, pos.y + 52.0f * scale));
        draw_panel(list, pos, ImVec2(pos.x + width, pos.y + 52.0f * scale),
                   selected ? p.card_selected : (hovered ? p.card_hover : p.card),
                   selected ? mix(p.accent, p.void_, 0.55f) : p.panel_border, 0.0f,
                   metrics().radius_md * scale, 1.0f);
        text_at(list, ImVec2(pos.x + 16.0f * scale, pos.y + 16.0f * scale), p.ink,
                provider.display_name.c_str(), type_scale().body);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          state.selected_provider_id = provider.idp_id;
        }
        ImGui::Dummy(ImVec2(width, 52.0f * scale));
        ImGui::PopID();
      }
      ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
      ImGui::BeginDisabled(state.logging_in);
      if (button_accent(tr("auth.actions.signInWithLink"), ImVec2(-1.0f, 42.0f * scale))) {
        app.start_device_login();
      }
      ImGui::EndDisabled();
      ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
      wrapped_text(tr("auth.link.preparingDescription"), ImGui::GetContentRegionAvail().x,
                   p.ink_muted);
    }
  }

  ImGui::EndChild();
}

} // namespace app
} // namespace onow
