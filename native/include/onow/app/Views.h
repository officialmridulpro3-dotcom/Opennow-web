// View entry points. Each one draws one screen into the content rect the shell
// hands it; the shell (rail / header / status bar) is drawn by App.
#pragma once

#include "onow/app/AppState.h"

namespace onow {
namespace app {

class App;

void draw_login_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_home_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_library_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_details_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_playtime_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_settings_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_stream_view(App& app, AppState& state, const ImVec2& min, const ImVec2& max);

// Overlays drawn on top of whatever view is active.
void draw_toasts(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_modal(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_queue_server_modal(App& app, AppState& state, const ImVec2& min, const ImVec2& max);

// The shell chrome.
void draw_rail(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_header(App& app, AppState& state, const ImVec2& min, const ImVec2& max);
void draw_status_bar(App& app, AppState& state, const ImVec2& min, const ImVec2& max);

} // namespace app
} // namespace onow
