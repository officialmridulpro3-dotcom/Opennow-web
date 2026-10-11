// OpenNOW native client — application entry point.

#pragma once

#include "App.xaml.g.h"

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
