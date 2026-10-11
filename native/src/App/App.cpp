// OpenNOW native client — application entry point.

#include "pch.h"

#include "App.h"
// cppwinrt's activation-factory glue, named after the namespace-qualified
// class. Both spellings are tried so the build does not hang on which one this
// version of cppwinrt picks.
#if __has_include("App.g.cpp")
#include "App.g.cpp"
#endif
#include "MainWindow.h"

#include <opennow/core/Logging.hpp>
#include <opennow/core/Settings.hpp>
#include <opennow/dx/StreamSurface.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::OpenNOW::implementation {

App::App() {
    // XAML type resolution for user controls declared only in markup.
    InitializeComponent();

#if defined(_DEBUG) && !defined(DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION)
    UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& event) {
        if (IsDebuggerPresent()) {
            const auto message = event.Message();
            __debugbreak();
        }
    });
#endif
}

void App::ConfigureLogging() {
    const auto settings = opennow::core::SettingsStore::Instance().Load();
    const auto level = settings.interface_.effects == opennow::core::EffectsProfile::Reduced
                           ? opennow::core::LogLevel::Warn
                           : opennow::core::LogLevel::Info;
    opennow::core::Logger::Instance().Configure(opennow::core::Paths::LogDirectory(), level);
    opennow::core::LogInfo("App", "OpenNOW native client starting.");
}

void App::ApplyStartupTheme() {
    // The client is dark-only by design: the streaming surface is calibrated
    // for a dark surround, and the web client makes the same choice.
    if (auto root = Resources().TryLookup(box_value(L"OpenNowBackgroundBrush"))) {
        // no-op: brushes come from the merged dictionaries above
    }
}

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
    ConfigureLogging();

    const auto adapter = opennow::dx::QueryAdapterInfo();
    if (adapter.directx10Capable) {
        opennow::core::LogInfo("App", "DirectX 10 device present at feature level " +
                                          std::to_string(adapter.d3d10FeatureLevel) + ".");
    } else {
        opennow::core::LogWarn("App", "No DirectX 10 device found; the stream surface will fall "
                                      "back to a software presenter.");
    }

    // A DirectX 10-class GPU gets the reduced-effects profile automatically,
    // matching what the web client does for constrained devices.
    auto settings = opennow::core::SettingsStore::Instance().Load();
    if (settings.interface_.lowEffectsAuto && adapter.d3d10FeatureLevel <= 1000) {
        settings.interface_.effects = opennow::core::EffectsProfile::Reduced;
        opennow::core::SettingsStore::Instance().Save(settings);
    }

    window_ = make<MainWindow>();

    // RequestedTheme is applied to the window's content so the root element and
    // every child resolve the dark theme dictionaries.
    if (auto content = window_.try_as<Microsoft::UI::Xaml::FrameworkElement>()) {
        content.RequestedTheme(ElementTheme::Dark);
    }

    window_.Activate();
}

}  // namespace winrt::OpenNOW::implementation
