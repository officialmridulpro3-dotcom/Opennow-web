// OpenNOW native client — application entry point.

#pragma once

#include "App.g.h"

namespace winrt::OpenNOW::implementation {

class App : public AppT<App> {
public:
    App();

    void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

private:
    void ConfigureLogging();
    void ApplyStartupTheme();

    winrt::OpenNOW::MainWindow window_{nullptr};
};

}  // namespace winrt::OpenNOW::implementation

namespace winrt::OpenNOW::factory_implementation {

class App : public AppT<App, implementation::App> {};

}  // namespace winrt::OpenNOW::factory_implementation
