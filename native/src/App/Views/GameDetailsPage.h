// OpenNOW native client — game details page.

#pragma once

#include "GameDetailsPage.g.h"

#include <opennow/core/Types.hpp>

namespace winrt::OpenNOW::Views::implementation {

class GameDetailsPage : public GameDetailsPageT<GameDetailsPage> {
public:
    GameDetailsPage();

    void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
    void OnBack(Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnPlay(Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnPickRegion(Windows::Foundation::IInspectable const& sender,
                      Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnCancelRegion(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnRegionSelected(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

private:
    winrt::fire_and_forget LoadAsync(hstring gameId);
    winrt::fire_and_forget MeasureRegionsAsync();
    void PopulateMeta();

    std::optional<opennow::core::Game> game_;
    std::vector<opennow::core::Region> regions_;
    std::string selectedRegionUrl_;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class GameDetailsPage : public GameDetailsPageT<GameDetailsPage, implementation::GameDetailsPage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
