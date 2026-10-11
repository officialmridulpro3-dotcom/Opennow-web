// OpenNOW native client — sign-in page.

#include "pch.h"

#include "LoginPage.h"
#include "LoginPage.g.cpp"

#include <chrono>
#include <thread>

#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.System.h>

#include "Services/AppServices.h"
#include <opennow/core/Logging.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::System;

namespace winrt::OpenNOW::implementation {

LoginPage::LoginPage() {
    InitializeComponent();
    BeginDeviceLoginAsync();
}

winrt::event_token LoginPage::SignedIn(
    Windows::Foundation::TypedEventHandler<Windows::Foundation::IInspectable,
                                           Windows::Foundation::IInspectable> const& handler) {
    return signedInEvent_.add(handler);
}

void LoginPage::SignedIn(winrt::event_token const& token) noexcept { signedInEvent_.remove(token); }

winrt::fire_and_forget LoginPage::BeginDeviceLoginAsync() {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();

    StatusText().Text(L"Contacting NVIDIA…");
    ErrorText().Visibility(Visibility::Collapsed);

    co_await winrt::resume_background();
    const auto challenge = services.StartDeviceLogin(0);
    const auto providers = services.providers();

    co_await winrt::resume_foreground(DispatcherQueue());

    // Provider list is only known after the first discovery call.
    if (ProviderBox().Items().Size() == 0) {
        for (const auto& provider : providers) {
            ProviderBox().Items().Append(box_value(to_hstring(provider.displayName)));
        }
        if (ProviderBox().Items().Size() > 0) ProviderBox().SelectedIndex(0);
    }

    if (!challenge.ok()) {
        ShowError(to_hstring(challenge.error));
        co_return;
    }

    challenge_ = *challenge;
    UserCodeText().Text(to_hstring(challenge_->userCode));
    StatusText().Text(L"Waiting for approval…");
    PollingRing().IsActive(true);

    PollAsync();
}

winrt::fire_and_forget LoginPage::PollAsync() {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();
    if (!challenge_ || polling_) co_return;

    polling_ = true;
    auto challenge = *challenge_;

    co_await winrt::resume_background();

    opennow::core::gfn::DevicePollResult result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (std::chrono::steady_clock::now() < deadline) {
        result = services.PollForTokens(challenge);
        if (result.status != opennow::core::gfn::DevicePollStatus::Pending) break;
        std::this_thread::sleep_for(std::chrono::seconds(challenge.intervalSeconds));
    }

    if (result.status != opennow::core::gfn::DevicePollStatus::Success) {
        const std::string message = result.errorDescription.empty()
                                        ? "Sign-in was not completed."
                                        : result.errorDescription;
        co_await winrt::resume_foreground(DispatcherQueue());
        polling_ = false;
        PollingRing().IsActive(false);
        ShowError(to_hstring(message));
        co_return;
    }

    const auto completed = services.CompleteSignIn(result.tokens);

    co_await winrt::resume_foreground(DispatcherQueue());
    polling_ = false;

    if (!completed.ok()) {
        PollingRing().IsActive(false);
        ShowError(to_hstring(completed.error));
        co_return;
    }

    StatusText().Text(L"Signed in.");
    PollingRing().IsActive(false);
    opennow::core::LogInfo("Login", "Sign-in completed.");
    signedInEvent_(*this, nullptr);
}

void LoginPage::OnProviderChanged(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    if (ProviderBox().SelectedIndex() < 0) return;
    challenge_.reset();
    BeginDeviceLoginAsync();
}

void LoginPage::OnOpenBrowser(Windows::Foundation::IInspectable const&,
                              Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!challenge_) return;
    // The complete URI pre-fills the code, so the user only has to approve.
    const auto uri = Windows::Foundation::Uri(
        to_hstring(challenge_->verificationUriComplete.empty()
                       ? challenge_->verificationUri
                       : challenge_->verificationUriComplete));
    Launcher::LaunchUriAsync(uri);
}

void LoginPage::OnCopyCode(Windows::Foundation::IInspectable const&,
                           Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!challenge_) return;
    DataPackage package;
    package.SetText(UserCodeText().Text());
    Clipboard::SetContent(package);
}

void LoginPage::OnRestart(Windows::Foundation::IInspectable const&,
                          Microsoft::UI::Xaml::RoutedEventArgs const&) {
    challenge_.reset();
    BeginDeviceLoginAsync();
}

void LoginPage::ShowError(hstring const& message) {
    ErrorText().Text(message);
    ErrorText().Visibility(Visibility::Visible);
    StatusText().Text(L"Sign-in failed.");
    RestartButton().Visibility(Visibility::Visible);
}

}  // namespace winrt::OpenNOW::implementation
