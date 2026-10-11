// OpenNOW native client — main window (app shell).

#pragma once

#include "MainWindow.g.h"

#include "Services/AppServices.h"

namespace winrt::OpenNOW::implementation {

class MainWindow : public MainWindowT<MainWindow> {
public:
    MainWindow();

    // ---- navigation --------------------------------------------------------------
    void OnNavClicked(Windows::Foundation::IInspectable const& sender,
                      Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnSearchTextChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
                             Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);
    void OnSearchSubmitted(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
                           Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const& args);
    void OnAccountClicked(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::RoutedEventArgs const& args);

    void NavigateTo(hstring const& page);

    // Called by the pages when they need the shell to react.
    void ShowLoginOverlay(bool show);
    void SetStatus(hstring const& text, bool connected);
    void UpdateHeaderForPage(hstring const& page);

private:
    winrt::fire_and_forget LoadAsync();
    void ApplyAccentTheme();
    void WireSignInHandlers();
    void WireStreamHandlers();
    void UpdateAccountUi();

    Microsoft::UI::Xaml::Controls::Button ActiveNavButton();
    void SelectNavButton(Microsoft::UI::Xaml::Controls::Button const& button);

    hstring currentPage_ = L"home";
    bool loginOverlayVisible_ = false;
};

}  // namespace winrt::OpenNOW::implementation

namespace winrt::OpenNOW::factory_implementation {

class MainWindow : public MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::OpenNOW::factory_implementation
