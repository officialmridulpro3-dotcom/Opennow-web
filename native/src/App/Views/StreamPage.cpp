// OpenNOW native client — streaming page.

#include "pch.h"

#include "StreamPage.h"
#include "Views.StreamPage.g.cpp"

#include <winrt/Microsoft.UI.Windowing.h>

#include "Services/AppServices.h"
#include <opennow/core/Logging.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Input;
using namespace winrt::Microsoft::UI::Dispatching;
using namespace Windows::System;

namespace winrt::OpenNOW::Views::implementation {

StreamPage::StreamPage() {
    InitializeComponent();

    KeyDown({this, &StreamPage::OnKeyDown});
    PointerMoved({this, &StreamPage::OnPointerMoved});
    // Key events only arrive when the page itself has focus.
    Loaded([this](auto&&, auto&&) { Focus(FocusState::Programmatic); });

    controlTimer_ = DispatcherQueue().CreateTimer();
    controlTimer_.Interval(std::chrono::seconds(3));
    controlTimer_.Tick([this](auto&&, auto&&) { ControlBar().Visibility(Visibility::Collapsed); });
}

StreamPage::~StreamPage() { opennow::app::AppServices::Instance().stream().Stop(); }

void StreamPage::OnNavigatedTo(Microsoft::UI::Xaml::Navigation::NavigationEventArgs const& args) {
    if (auto parameter = args.Parameter().try_as<hstring>()) {
        const std::string combined = winrt::to_string(*parameter);
        const std::size_t separator = combined.find('\n');
        gameId_ = combined.substr(0, separator);
        gameTitle_ = separator == std::string::npos ? gameId_ : combined.substr(separator + 1);
    }
    StartSession();
}

void StreamPage::StartSession() {
    auto& services = opennow::app::AppServices::Instance();
    const auto& settings = services.settings();

    sessionStarted_ = std::chrono::steady_clock::now();
    LoadingOverlay().Visibility(Visibility::Visible);
    LoadingRing().IsActive(true);

    opennow::app::StreamCallbacks callbacks;
    callbacks.onProgress = [this](const opennow::app::StreamProgress& progress) {
        OnProgress(progress);
    };
    callbacks.onFrame = [this](opennow::app::VideoFrameData&& frame) { OnFrame(std::move(frame)); };
    callbacks.onStats = [this](const opennow::core::StreamStats& stats) { OnStats(stats); };
    callbacks.onError = [this](const std::string& message) { OnError(message); };

    // The stream surface is sized for the requested resolution; the renderer
    // letterboxes it inside whatever size the panel ends up being.
    int width = 1920;
    int height = 1080;
    if (const std::size_t separator = settings.stream.resolution.find('x');
        separator != std::string::npos) {
        width = std::stoi(settings.stream.resolution.substr(0, separator));
        height = std::stoi(settings.stream.resolution.substr(separator + 1));
    }
    Surface().Begin(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    const std::string token = services.tokens().accessToken;
    const std::string base = services.preferredRegionUrl().empty()
                                 ? services.streamingBaseUrl()
                                 : services.preferredRegionUrl();
    const std::string zone = services.settings().preferredZone.empty()
                                 ? std::string("auto")
                                 : services.settings().preferredZone;

    opennow::core::Game game;
    game.id = gameId_;
    game.title = gameTitle_;
    game.internalTitle = gameTitle_;

    services.stream().Start(game, settings.stream, token, base, zone, std::move(callbacks));
}

void StreamPage::OnProgress(const opennow::app::StreamProgress& progress) {
    // Called from the session worker thread; marshal to the UI thread.
    DispatcherQueue().TryEnqueue([this, progress] {
        PhaseText().Text(hstring(PhaseLabel(progress.phase)));
        DetailText().Text(to_hstring(progress.detail));

        if (progress.queuePosition.has_value()) {
            QueueText().Text(to_hstring("Position " + std::to_string(*progress.queuePosition)));
            QueueText().Visibility(Visibility::Visible);
        } else if (progress.phase == opennow::app::StreamPhase::Streaming) {
            QueueText().Visibility(Visibility::Collapsed);
        }

        if (progress.phase == opennow::app::StreamPhase::Streaming) {
            LoadingOverlay().Visibility(Visibility::Collapsed);
            LoadingRing().IsActive(false);
            ControlBar().Visibility(Visibility::Visible);
            ShowControls();
            HideControlsSoon();
        } else if (progress.phase == opennow::app::StreamPhase::Failed) {
            LoadingRing().IsActive(false);
            CancelButton().Content(box_value(L"Back"));
        }
    });
}

void StreamPage::OnFrame(opennow::app::VideoFrameData&& frame) {
    // Frames arrive on the transport thread. Copy into WinRT arrays and hand
    // them to the DirectX surface on the UI thread; the upload itself happens
    // inside the renderer, so this stays cheap.
    DispatcherQueue().TryEnqueue([this, frame = std::move(frame)]() mutable {
        if (stopping_) return;
        winrt::array_view<const uint8_t> luma(frame.luma);
        winrt::array_view<const uint8_t> chroma(frame.chroma);
        Surface().SubmitNv12(luma, static_cast<uint32_t>(frame.lumaStride), chroma,
                             static_cast<uint32_t>(frame.chromaStride),
                             static_cast<uint32_t>(frame.width),
                             static_cast<uint32_t>(frame.height), frame.fullRange);
    });
}

void StreamPage::OnStats(const opennow::core::StreamStats& stats) {
    DispatcherQueue().TryEnqueue([this, stats] {
        ResolutionValue().Text(to_hstring(std::to_string(stats.width) + " × " +
                                          std::to_string(stats.height)));
        FpsValue().Text(to_hstring(std::to_string(stats.fps)));
        BitrateValue().Text(to_hstring(std::to_string(stats.bitrateKbps / 1000) + " Mbps"));
        LatencyValue().Text(to_hstring(std::to_string(stats.latencyMs) + " ms"));
        LossValue().Text(to_hstring(std::to_string(stats.packetLossPercent) + " %"));
    });
}

void StreamPage::OnError(const std::string& message) {
    DispatcherQueue().TryEnqueue([this, message] {
        PhaseText().Text(L"Could not start the stream");
        DetailText().Text(to_hstring(message));
        LoadingRing().IsActive(false);
        CancelButton().Content(box_value(L"Back"));
        opennow::core::LogError("Stream", message);
    });
}

void StreamPage::OnCancel(Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&) {
    stopping_ = true;
    opennow::app::AppServices::Instance().stream().Stop();
    if (Frame().CanGoBack()) Frame().GoBack();
}

void StreamPage::OnStop(Windows::Foundation::IInspectable const&,
                        Microsoft::UI::Xaml::RoutedEventArgs const&) {
    stopping_ = true;
    auto& services = opennow::app::AppServices::Instance();
    services.stream().Stop();

    // Playtime is recorded even for a short session: it is the only place the
    // app knows how long a game was actually played.
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::steady_clock::now() - sessionStarted_)
                             .count();
    if (elapsed > 5) services.RecordSession(gameId_, gameTitle_, static_cast<std::int64_t>(elapsed));

    ExitPrompt().Visibility(Visibility::Collapsed);
    if (Frame().CanGoBack()) Frame().GoBack();
}

void StreamPage::OnDismissExit(Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ExitPrompt().Visibility(Visibility::Collapsed);
}

void StreamPage::OnToggleStats(Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&) {
    statsVisible_ = !statsVisible_;
    StatsPanel().Visibility(statsVisible_ ? Visibility::Visible : Visibility::Collapsed);
}

void StreamPage::OnToggleMicrophone(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    microphoneOn_ = !microphoneOn_;
    auto& services = opennow::app::AppServices::Instance();
    services.settings().audio.microphone = microphoneOn_;
    services.SaveSettings();
}

void StreamPage::OnToggleFullscreen(Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    // WinUI 3 exposes the presenter through the window's AppWindow.
    if (auto window = Microsoft::UI::Windowing::AppWindow::TryCreateForCurrentThread()) {
        const auto presenter = window.Presenter().as<Microsoft::UI::Windowing::FullScreenPresenter>();
        if (presenter != nullptr) {
            window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::Default);
        } else {
            window.SetPresenter(Microsoft::UI::Windowing::AppWindowPresenterKind::FullScreen);
        }
    }
}

void StreamPage::OnKeyDown(Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args) {
    const auto shift = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(
                           VirtualKey::Shift)
                           .has_flag(Windows::UI::Core::CoreVirtualKeyStates::Down);
    const auto control = Microsoft::UI::Input::InputKeyboardSource::GetKeyStateForCurrentThread(
                             VirtualKey::Control)
                             .has_flag(Windows::UI::Core::CoreVirtualKeyStates::Down);

    if (control && shift && args.Key() == VirtualKey::S) {
        OnToggleStats(nullptr, nullptr);
        args.Handled(true);
    } else if (args.Key() == VirtualKey::F11) {
        OnToggleFullscreen(nullptr, nullptr);
        args.Handled(true);
    } else if (args.Key() == VirtualKey::Escape) {
        if (ExitPrompt().Visibility() == Visibility::Visible) {
            ExitPrompt().Visibility(Visibility::Collapsed);
        } else {
            ExitPrompt().Visibility(Visibility::Visible);
            ShowControls();
        }
        args.Handled(true);
    }
}

void StreamPage::OnPointerMoved(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
    ShowControls();
    HideControlsSoon();
}

void StreamPage::ShowControls() {
    ControlBar().Visibility(Visibility::Visible);
    controlTimer_.Stop();
}

void StreamPage::HideControlsSoon() {
    controlTimer_.Stop();
    controlTimer_.Start();
}

}  // namespace winrt::OpenNOW::Views::implementation
