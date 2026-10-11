// OpenNOW native client — playtime page.

#pragma once

#include "Views.PlaytimeViewModel.g.h"
#include "PlaytimePage.g.h"

#include <opennow/core/Types.hpp>

namespace winrt::OpenNOW::Views::implementation {

struct PlaytimeViewModel : PlaytimeViewModelT<PlaytimeViewModel> {
    PlaytimeViewModel() = default;
    PlaytimeViewModel(const opennow::core::PlaytimeEntry& entry);

    hstring GameId();
    hstring Title();
    hstring TotalLabel();
    hstring SessionsLabel();
    hstring LastPlayedLabel();

private:
    hstring gameId_;
    hstring title_;
    hstring totalLabel_;
    hstring sessionsLabel_;
    hstring lastPlayedLabel_;
};

class PlaytimePage : public PlaytimePageT<PlaytimePage> {
public:
    PlaytimePage();

    void Refresh();

private:
    void Populate();

    std::vector<opennow::core::PlaytimeEntry> entries_;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class PlaytimePage : public PlaytimePageT<PlaytimePage, implementation::PlaytimePage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
