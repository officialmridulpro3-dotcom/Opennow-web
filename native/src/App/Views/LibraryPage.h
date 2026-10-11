// OpenNOW native client — library page.

#pragma once

#include "Views.LibraryPage.g.h"

#include "HomePage.h"

#include <opennow/core/Types.hpp>

namespace winrt::OpenNOW::Views::implementation {

class LibraryPage : public LibraryPageT<LibraryPage> {
public:
    LibraryPage();

    void RunSearch(hstring const& query);

    void OnFilterChanged(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnGameClicked(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args);
    // Fades each card in as its container is realised instead of popping in the
    // whole grid at once.
    void OnContainerChanging(
        Windows::Foundation::IInspectable const& sender,
        Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args);

private:
    winrt::fire_and_forget LoadAsync();
    void ApplyFilters();

    std::vector<opennow::core::Game> games_;
    std::string activeQuery_;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class LibraryPage : public LibraryPageT<LibraryPage, implementation::LibraryPage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
