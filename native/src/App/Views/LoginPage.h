// OpenNOW native client — sign-in page.

#pragma once

#include "Views.LoginPage.g.h"

#include <opennow/core/gfn/Auth.hpp>

namespace winrt::OpenNOW::Views::implementation {

class LoginPage : public LoginPageT<LoginPage> {
public:
    LoginPage();

    winrt::event_token SignedIn(Windows::Foundation::TypedEventHandler<
                                Windows::Foundation::IInspectable,
                                Windows::Foundation::IInspectable> const& handler);
    void SignedIn(winrt::event_token const& token) noexcept;

    // ---- event handlers ----------------------------------------------------------
    void OnProviderChanged(Windows::Foundation::IInspectable const& sender,
                           Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
    void OnOpenBrowser(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnCopyCode(Windows::Foundation::IInspectable const& sender,
                    Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnRestart(Windows::Foundation::IInspectable const& sender,
                   Microsoft::UI::Xaml::RoutedEventArgs const& args);

private:
    winrt::fire_and_forget BeginDeviceLoginAsync();
    winrt::fire_and_forget PollAsync();
    void ShowError(hstring const& message);

    std::optional<opennow::core::gfn::DeviceLoginChallenge> challenge_;
    bool polling_ = false;
    winrt::event<Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable,
                                                        Windows::Foundation::IInspectable>>
        signedInEvent_;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class LoginPage : public LoginPageT<LoginPage, implementation::LoginPage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
