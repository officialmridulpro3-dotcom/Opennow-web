// OpenNOW native client — playtime page.

#include "pch.h"

#include "PlaytimePage.h"
#include "Views.PlaytimePage.g.cpp"

#include <algorithm>
#include <sstream>

#include "Services/AppServices.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::OpenNOW::Views::implementation {

namespace {

// "7h 42m" — the web client's format, kept so the two clients agree.
std::string FormatDuration(std::int64_t seconds) {
    const std::int64_t hours = seconds / 3600;
    const std::int64_t minutes = (seconds % 3600) / 60;
    if (hours > 0) return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    return std::to_string(minutes) + "m";
}

std::string FormatLastPlayed(std::int64_t unixSeconds) {
    if (unixSeconds <= 0) return "never";
    const std::time_t time = static_cast<std::time_t>(unixSeconds);
    std::tm parts{};
    localtime_s(&parts, &time);
    std::ostringstream stream;
    stream << std::put_time(&parts, "%d %b %Y");
    return stream.str();
}

}  // namespace

PlaytimeViewModel::PlaytimeViewModel(const opennow::core::PlaytimeEntry& entry)
    : gameId_(to_hstring(entry.gameId)),
      title_(to_hstring(entry.title)),
      totalLabel_(to_hstring(FormatDuration(entry.totalSeconds))),
      sessionsLabel_(to_hstring(std::to_string(entry.sessions) +
                                (entry.sessions == 1 ? " session" : " sessions"))),
      lastPlayedLabel_(to_hstring(FormatLastPlayed(entry.lastPlayedAt))) {}

hstring PlaytimeViewModel::GameId() { return gameId_; }
hstring PlaytimeViewModel::Title() { return title_; }
hstring PlaytimeViewModel::TotalLabel() { return totalLabel_; }
hstring PlaytimeViewModel::SessionsLabel() { return sessionsLabel_; }
hstring PlaytimeViewModel::LastPlayedLabel() { return lastPlayedLabel_; }

PlaytimePage::PlaytimePage() {
    InitializeComponent();
    Refresh();
}

void PlaytimePage::Refresh() {
    entries_ = opennow::app::AppServices::Instance().playtime();
    std::sort(entries_.begin(), entries_.end(),
              [](const opennow::core::PlaytimeEntry& a, const opennow::core::PlaytimeEntry& b) {
                  return a.lastPlayedAt > b.lastPlayedAt;
              });
    Populate();
}

void PlaytimePage::Populate() {
    auto models = single_threaded_observable_vector<PlaytimeViewModel>();
    std::int64_t totalSeconds = 0;
    std::int64_t totalSessions = 0;
    for (const auto& entry : entries_) {
        totalSeconds += entry.totalSeconds;
        totalSessions += entry.sessions;
        models.Append(make<PlaytimeViewModel>(entry));
    }

    PlaytimeList().ItemsSource(models);
    TotalTimeText().Text(to_hstring(FormatDuration(totalSeconds)));
    GamesCountText().Text(to_hstring(std::to_string(entries_.size())));
    SessionsCountText().Text(to_hstring(std::to_string(totalSessions)));

    const bool empty = entries_.empty();
    PlaytimeList().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
    EmptyText().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
}

}  // namespace winrt::OpenNOW::Views::implementation
