// OpenNOW native client — home page.

#pragma once

// The view models come first: ShelfViewModel instantiates
// IObservableVector<OpenNOW::Views::GameViewModel>, which asserts on an incomplete type.
#include "Views.GameViewModel.g.h"
#include "Views.ShelfViewModel.g.h"
#include "HomePage.g.h"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include <opennow/core/Types.hpp>

namespace winrt::OpenNOW::Views::implementation {

// Projection of a game for XAML data binding. WinUI binds to WinRT properties,
// so the Core model gets mirrored into a runtime class.
struct GameViewModel : GameViewModelT<GameViewModel> {
    GameViewModel() = default;
    GameViewModel(const opennow::core::Game& game);

    hstring Id();
    hstring Title();
    hstring PlatformLabel();
    hstring PosterUrl();
    hstring HeroUrl();
    hstring Genres();
    bool Owned();

private:
    hstring id_;
    hstring title_;
    hstring platformLabel_;
    hstring posterUrl_;
    hstring heroUrl_;
    hstring genres_;
    bool owned_ = false;
};

struct ShelfViewModel : ShelfViewModelT<ShelfViewModel> {
    ShelfViewModel() = default;

    hstring Title();
    void Title(hstring const& value);
    hstring CountLabel();
    void CountLabel(hstring const& value);
    Windows::Foundation::Collections::IObservableVector<OpenNOW::Views::GameViewModel> Games();
    void Games(Windows::Foundation::Collections::IObservableVector<OpenNOW::Views::GameViewModel> const& value);

private:
    hstring title_;
    hstring countLabel_;
    Windows::Foundation::Collections::IObservableVector<OpenNOW::Views::GameViewModel> games_{nullptr};
};

class HomePage : public HomePageT<HomePage> {
public:
    HomePage();

    void RefreshFromServices();

    void OnGameCardClicked(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnSpotlightPlay(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnSpotlightDetails(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);

private:
    winrt::fire_and_forget LoadAsync();
    void OpenDetails(const std::string& gameId);

    std::string spotlightId_;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class HomePage : public HomePageT<HomePage, implementation::HomePage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
