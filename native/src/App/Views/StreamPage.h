// OpenNOW native client — streaming page.

#pragma once

#include "StreamPage.g.h"

#include <chrono>
#include <string>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Input.h>

namespace winrt::OpenNOW::implementation {

class StreamPage : public StreamPageT<StreamPage> {
public:
    StreamPage();
    ~StreamPage();

    void OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args);
    void OnCancel(Windows::Foundation::IInspectable const& sender,
                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnStop(Windows::Foundation::IInspectable const& sender,
                Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnDismissExit(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnToggleStats(Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnToggleMicrophone(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnToggleFullscreen(Windows::Foundation::IInspectable const& sender,
                            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    // Keyboard shortcuts (Ctrl+Shift+S, F11, Escape).
    void OnKeyDown(Windows::Foundation::IInspectable const& sender,
                   Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args);
    void OnPointerMoved(Windows::Foundation::IInspectable const& sender,
                        Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);

private:
    void StartSession();
    void OnProgress(const opennow::app::StreamProgress& progress);
    void OnFrame(opennow::app::VideoFrameData&& frame);
    void OnStats(const opennow::core::StreamStats& stats);
    void OnError(const std::string& message);
    void ShowControls();
    void HideControlsSoon();

    std::string gameId_;
    std::string gameTitle_;
    std::chrono::steady_clock::time_point sessionStarted_{};
    bool statsVisible_ = false;
    bool microphoneOn_ = false;
    bool stopping_ = false;
    Microsoft::UI::Dispatching::DispatcherQueueTimer controlTimer_{nullptr};
};

}  // namespace winrt::OpenNOW::implementation

namespace winrt::OpenNOW::factory_implementation {

class StreamPage : public StreamPageT<StreamPage, implementation::StreamPage> {};

}  // namespace winrt::OpenNOW::factory_implementation
