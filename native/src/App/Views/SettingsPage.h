// OpenNOW native client — settings page.

#pragma once

#include "Views.SettingsPage.g.h"

namespace winrt::OpenNOW::Views::implementation {

class SettingsPage : public SettingsPageT<SettingsPage> {
public:
    SettingsPage();

    // ---- section plumbing --------------------------------------------------------
    void OnSectionChanged(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);
    void OnSearchChanged(Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
                         Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args);

    // ---- setting handlers ----------------------------------------------------------
    void OnStreamSettingChanged(Windows::Foundation::IInspectable const& sender,
                                Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnBitrateChanged(Windows::Foundation::IInspectable const& sender,
                          Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnSharpeningChanged(Windows::Foundation::IInspectable const& sender,
                             Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnGameSettingChanged(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnAudioSettingChanged(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnVolumeChanged(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnInputSettingChanged(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnSensitivityChanged(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnInterfaceSettingChanged(Windows::Foundation::IInspectable const& sender,
                                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnAccentChanged(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnLocaleChanged(Windows::Foundation::IInspectable const& sender,
                         Microsoft::UI::Xaml::RoutedEventArgs const& args);

    void OnSignOut(Windows::Foundation::IInspectable const& sender,
                   Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnRefreshSubscription(Windows::Foundation::IInspectable const& sender,
                               Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnOpenLogs(Windows::Foundation::IInspectable const& sender,
                    Microsoft::UI::Xaml::RoutedEventArgs const& args);

private:
    void LoadFromSettings();
    void ShowSection(hstring const& tag);
    void Save();

    bool loading_ = true;
};

}  // namespace winrt::OpenNOW::Views::implementation

namespace winrt::OpenNOW::Views::factory_implementation {

class SettingsPage : public SettingsPageT<SettingsPage, implementation::SettingsPage> {};

}  // namespace winrt::OpenNOW::Views::factory_implementation
