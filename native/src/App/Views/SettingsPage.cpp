// OpenNOW native client — settings page.

#include "pch.h"

#include "SettingsPage.h"
#include "SettingsPage.g.cpp"

#include <winrt/Windows.System.h>

#include "Services/AppServices.h"
#include <opennow/core/Logging.hpp>
#include <opennow/core/Paths.hpp>
#include <opennow/dx/StreamSurface.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace Windows::System;

namespace winrt::OpenNOW::implementation {

namespace {
int IndexOf(const std::vector<std::string>& values, const std::string& value) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == value) return static_cast<int>(index);
    }
    return -1;
}
}  // namespace

SettingsPage::SettingsPage() {
    InitializeComponent();
    LoadFromSettings();
    ShowSection(L"stream");
}

void SettingsPage::LoadFromSettings() {
    loading_ = true;
    auto& services = opennow::app::AppServices::Instance();
    const auto& settings = services.settings();

    // ---- stream ----
    static const std::vector<std::string> kResolutions = {"1280x720", "1920x1080", "2560x1440",
                                                          "3840x2160"};
    const int resolutionIndex = IndexOf(kResolutions, settings.stream.resolution);
    ResolutionBox().SelectedIndex(resolutionIndex >= 0 ? resolutionIndex : 1);

    static const std::vector<int> kFrameRates = {30, 60, 120, 240};
    int fpsIndex = 1;
    for (std::size_t index = 0; index < kFrameRates.size(); ++index) {
        if (kFrameRates[index] == settings.stream.fps) fpsIndex = static_cast<int>(index);
    }
    FpsBox().SelectedIndex(fpsIndex);

    BitrateSlider().Value(settings.stream.maxBitrateMbps);
    BitrateLabel().Text(to_hstring(std::to_string(settings.stream.maxBitrateMbps) + " Mbps"));
    CodecBox().SelectedIndex(static_cast<int>(settings.stream.codec));
    ColorQualityBox().SelectedIndex(static_cast<int>(settings.stream.colorQuality));
    SharpeningSlider().Value(settings.stream.sharpening);
    AdaptiveToggle().IsOn(settings.stream.adjustForPoorNetwork);
    GsyncToggle().IsOn(settings.stream.enableCloudGsync);
    ReflexToggle().IsOn(settings.stream.reflex);

    // ---- game ----
    GameLanguageBox().Text(to_hstring(settings.stream.gameLanguage));
    LaunchModeBox().SelectedIndex(
        settings.stream.appLaunchMode == opennow::core::AppLaunchMode::GamepadFriendly ? 1 : 0);

    // ---- audio ----
    VolumeSlider().Value(settings.audio.volume);
    MicrophoneToggle().IsOn(settings.audio.microphone);

    // ---- input ----
    GamepadToggle().IsOn(settings.input.gamepadEnabled);
    KeyboardLockToggle().IsOn(settings.input.keyboardLock);
    SensitivitySlider().Value(settings.input.mouseSensitivity);

    // ---- interface ----
    AccentBox().SelectedIndex(static_cast<int>(settings.interface_.accent));
    EffectsToggle().IsOn(settings.interface_.effects == opennow::core::EffectsProfile::Reduced);
    for (uint32_t index = 0; index < LocaleBox().Items().Size(); ++index) {
        if (auto item = LocaleBox().Items().GetAt(index).try_as<ComboBoxItem>()) {
            if (to_string(unbox_value<hstring>(item.Tag())) == settings.interface_.locale) {
                LocaleBox().SelectedIndex(static_cast<int32_t>(index));
                break;
            }
        }
    }

    // ---- account ----
    const auto profile = services.profile();
    AccountEmail().Text(to_hstring(profile.email.empty() ? profile.displayName : profile.email));
    const auto subscription = services.subscription();
    AccountTier().Text(to_hstring(subscription.tierName.empty() ? "Unknown"
                                                                : subscription.tierName));

    // ---- about ----
    const auto adapter = opennow::dx::QueryAdapterInfo();
    AdapterLabel().Text(to_hstring(
        "GPU: " + (adapter.description.empty() ? std::string("unknown")
                                               : winrt::to_string(adapter.description))));
    const std::string directx =
        adapter.directx10Capable
            ? "DirectX 10 capable — feature level " + std::to_string(adapter.d3d10FeatureLevel) +
                  ", " + std::to_string(adapter.dedicatedVideoMemory / (1024 * 1024)) + " MB video memory."
            : "No DirectX 10 device detected. Streaming will fall back to the software presenter.";
    DirectXLabel().Text(to_hstring(directx));

    loading_ = false;
}

void SettingsPage::Save() {
    if (loading_) return;
    opennow::app::AppServices::Instance().SaveSettings();
}

void SettingsPage::ShowSection(hstring const& tag) {
    auto show = [](FrameworkElement const& element, bool visible) {
        element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    };
    show(AccountSection(), tag == L"account");
    show(StreamSection(), tag == L"stream");
    show(GameSection(), tag == L"game");
    show(AudioSection(), tag == L"audio");
    show(InputSection(), tag == L"input");
    show(InterfaceSection(), tag == L"interface");
    show(AboutSection(), tag == L"about");
    show(ThanksSection(), tag == L"thanks");
}

void SettingsPage::OnSectionChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args) {
    if (auto item = args.SelectedItem().try_as<NavigationViewItem>()) {
        ShowSection(unbox_value<hstring>(item.Tag()));
    }
}

void SettingsPage::OnSearchChanged(
    Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
    Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args) {
    if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;

    // Searching shows every matching section at once instead of navigating to
    // one: the web client does the same, and it is faster to scan.
    const std::wstring needle = sender.Text().c_str();
    if (needle.empty()) {
        ShowSection(L"stream");
        return;
    }

    // Each entry resolves its element through a lambda: the section controls
    // are XAML-generated properties, not addressable lvalues.
    struct Entry {
        hstring tag;
        std::function<FrameworkElement()> element;
        std::wstring keywords;
    };

    Entry entries[] = {
        {L"account", [this] { return AccountSection(); }, L"account sign out email membership nvidia"},
        {L"stream", [this] { return StreamSection(); },
         L"stream resolution fps bitrate codec colour sharpening gsync reflex"},
        {L"game", [this] { return GameSection(); }, L"game language launch big picture"},
        {L"audio", [this] { return AudioSection(); }, L"audio volume microphone sound"},
        {L"input", [this] { return InputSection(); },
         L"input gamepad keyboard mouse sensitivity controller"},
        {L"interface", [this] { return InterfaceSection(); },
         L"interface accent colour effects theme language locale"},
        {L"about", [this] { return AboutSection(); }, L"about version gpu directx logs"},
        {L"thanks", [this] { return ThanksSection(); }, L"thanks credits contributors"},
    };

    bool any = false;
    for (auto& entry : entries) {
        const bool match = entry.keywords.find(needle) != std::wstring::npos;
        entry.element().Visibility(match ? Visibility::Visible : Visibility::Collapsed);
        any = any || match;
    }
    if (!any) ShowSection(L"stream");
}

void SettingsPage::OnStreamSettingChanged(Windows::Foundation::IInspectable const&,
                                          Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();

    if (auto item = ResolutionBox().SelectedItem().try_as<ComboBoxItem>()) {
        settings.stream.resolution = to_string(unbox_value<hstring>(item.Content()));
    }
    if (auto item = FpsBox().SelectedItem().try_as<ComboBoxItem>()) {
        settings.stream.fps = std::stoi(to_string(unbox_value<hstring>(item.Content())));
    }
    settings.stream.codec = static_cast<opennow::core::VideoCodec>(CodecBox().SelectedIndex());
    settings.stream.colorQuality =
        static_cast<opennow::core::ColorQuality>(ColorQualityBox().SelectedIndex());
    settings.stream.adjustForPoorNetwork = AdaptiveToggle().IsOn();
    settings.stream.enableCloudGsync = GsyncToggle().IsOn();
    settings.stream.reflex = ReflexToggle().IsOn();
    Save();
}

void SettingsPage::OnBitrateChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.stream.maxBitrateMbps = static_cast<int>(args.NewValue());
    BitrateLabel().Text(to_hstring(std::to_string(settings.stream.maxBitrateMbps) + " Mbps"));
    Save();
}

void SettingsPage::OnSharpeningChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.stream.sharpening = static_cast<int>(args.NewValue());
    Save();
}

void SettingsPage::OnGameSettingChanged(Windows::Foundation::IInspectable const&,
                                        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.stream.gameLanguage = to_string(GameLanguageBox().Text());
    settings.stream.appLaunchMode = LaunchModeBox().SelectedIndex() == 1
                                        ? opennow::core::AppLaunchMode::GamepadFriendly
                                        : opennow::core::AppLaunchMode::Default;
    Save();
}

void SettingsPage::OnAudioSettingChanged(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.audio.microphone = MicrophoneToggle().IsOn();
    Save();
}

void SettingsPage::OnVolumeChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args) {
    if (loading_) return;
    opennow::app::AppServices::Instance().settings().audio.volume =
        static_cast<int>(args.NewValue());
    Save();
}

void SettingsPage::OnInputSettingChanged(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.input.gamepadEnabled = GamepadToggle().IsOn();
    settings.input.keyboardLock = KeyboardLockToggle().IsOn();
    Save();
}

void SettingsPage::OnSensitivityChanged(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args) {
    if (loading_) return;
    opennow::app::AppServices::Instance().settings().input.mouseSensitivity = args.NewValue();
    Save();
}

void SettingsPage::OnInterfaceSettingChanged(Windows::Foundation::IInspectable const&,
                                             Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& settings = opennow::app::AppServices::Instance().settings();
    settings.interface_.effects = EffectsToggle().IsOn() ? opennow::core::EffectsProfile::Reduced
                                                         : opennow::core::EffectsProfile::Full;
    Save();
}

void SettingsPage::OnAccentChanged(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    auto& services = opennow::app::AppServices::Instance();
    services.settings().interface_.accent =
        static_cast<opennow::core::AccentTheme>(AccentBox().SelectedIndex());
    services.SaveSettings();

    // Re-point the accent brush in the live resource dictionary so the whole app
    // recolours without a restart.
    static const wchar_t* kKeys[] = {L"OpenNowAccentBrush", L"OpenNowAccentLimeBrush",
                                     L"OpenNowAccentVioletBrush", L"OpenNowAccentAmberBrush",
                                     L"OpenNowAccentRoseBrush"};
    const int index = AccentBox().SelectedIndex();
    if (index >= 0 && index < 5) {
        if (auto brush = Resources().TryLookup(box_value(kKeys[index]))
                             .try_as<Microsoft::UI::Xaml::Media::SolidColorBrush>()) {
            Resources().Insert(box_value(L"OpenNowAccentBrush"), brush);
        }
    }
}

void SettingsPage::OnLocaleChanged(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (loading_) return;
    if (auto item = LocaleBox().SelectedItem().try_as<ComboBoxItem>()) {
        auto& services = opennow::app::AppServices::Instance();
        services.settings().interface_.locale = to_string(unbox_value<hstring>(item.Tag()));
        services.SaveSettings();
    }
}

void SettingsPage::OnSignOut(Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&) {
    opennow::app::AppServices::Instance().SignOut();
    AccountEmail().Text(L"Not signed in");
    AccountTier().Text(L"");
}

void SettingsPage::OnRefreshSubscription(Windows::Foundation::IInspectable const&,
                                         Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto& services = opennow::app::AppServices::Instance();
    const auto lifetime = get_strong();
    [this, lifetime, &services]() -> winrt::fire_and_forget {
        co_await winrt::resume_background();
        const auto result = services.RefreshSubscription();
        co_await winrt::resume_foreground(DispatcherQueue());
        if (result.ok()) AccountTier().Text(to_hstring(result->tierName));
    }();
}

void SettingsPage::OnOpenLogs(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&) {
    const auto folder = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
                            to_hstring(opennow::core::Paths::LogDirectory()))
                            .get();
    Launcher::LaunchFolderAsync(folder);
}

}  // namespace winrt::OpenNOW::implementation
