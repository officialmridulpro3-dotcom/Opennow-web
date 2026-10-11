// OpenNOW native client — main window (app shell).

#include "pch.h"

#include "MainWindow.h"

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <opennow/core/Logging.hpp>

#include "Views/GameDetailsPage.h"
#include "Views/HomePage.h"
#include "Views/LibraryPage.h"
#include "Views/LoginPage.h"
#include "Views/PlaytimePage.h"
#include "Views/SettingsPage.h"
#include "Views/StreamPage.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace {

std::wstring Utf8ToWide(const std::string& text) { return winrt::to_hstring(text).c_str(); }

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

}  // namespace

namespace winrt::OpenNOW::implementation {

MainWindow::MainWindow() {
    InitializeComponent();

    // Custom title bar: the drag region is the empty part of the top row, and
    // the window buttons keep their system placement.
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(AppTitleBar());

    auto& services = opennow::app::AppServices::Instance();
    services.Initialize();

    ApplyAccentTheme();
    WireSignInHandlers();
    WireStreamHandlers();

    LoadAsync();
    NavigateTo(L"home");
}

winrt::fire_and_forget MainWindow::LoadAsync() {
    auto& services = opennow::app::AppServices::Instance();
    const auto lifetime = get_strong();

    co_await winrt::resume_background();

    const auto restored = services.RestoreSession();
    const bool signedIn = restored.ok();

    if (signedIn) {
        services.RefreshHome();
        services.RefreshLibrary();
        services.RefreshSubscription();
        services.RefreshRegions();
    }

    co_await winrt::resume_foreground(DispatcherQueue());

    ShowLoginOverlay(!signedIn);
    UpdateAccountUi();
    SetStatus(signedIn ? L"Connected" : L"Not signed in", signedIn);

    if (ContentFrame().Content()) {
        if (auto page = ContentFrame().Content().try_as<Views::HomePage>()) {
            page.RefreshFromServices();
        }
    }
}

void MainWindow::ApplyAccentTheme() {
    const auto& settings = opennow::app::AppServices::Instance().settings();
    // Accent switching re-points the single accent brush rather than swapping
    // dictionaries: WinUI resolves ThemeResource live, so every control that
    // uses OpenNowAccentBrush picks the new colour up immediately.
    Microsoft::UI::Xaml::Media::SolidColorBrush accentBrush{};
    switch (settings.interface_.accent) {
        case opennow::core::AccentTheme::Lime:
            accentBrush = Resources().TryLookup(box_value(L"OpenNowAccentLimeBrush"))
                              .as<Microsoft::UI::Xaml::Media::SolidColorBrush>();
            break;
        case opennow::core::AccentTheme::Violet:
            accentBrush = Resources().TryLookup(box_value(L"OpenNowAccentVioletBrush"))
                              .as<Microsoft::UI::Xaml::Media::SolidColorBrush>();
            break;
        case opennow::core::AccentTheme::Amber:
            accentBrush = Resources().TryLookup(box_value(L"OpenNowAccentAmberBrush"))
                              .as<Microsoft::UI::Xaml::Media::SolidColorBrush>();
            break;
        case opennow::core::AccentTheme::Rose:
            accentBrush = Resources().TryLookup(box_value(L"OpenNowAccentRoseBrush"))
                              .as<Microsoft::UI::Xaml::Media::SolidColorBrush>();
            break;
        default:
            return;  // Cyan is the dictionary default.
    }
    if (accentBrush != nullptr) {
        Resources().Insert(box_value(L"OpenNowAccentBrush"), accentBrush);
    }
}

void MainWindow::WireSignInHandlers() {
    // The login page owns the device-code flow; the shell only reacts.
    LoginOverlay().SignedIn([this](Windows::Foundation::IInspectable const&,
                                   Windows::Foundation::IInspectable const&) {
        ShowLoginOverlay(false);
        UpdateAccountUi();
        SetStatus(L"Connected", true);
        LoadAsync();
    });
}

void MainWindow::WireStreamHandlers() {
    // Stream start/stop is driven from the details page; the shell shows the
    // stream page and mirrors progress into the status bar.
    if (auto details = ContentFrame().Content().try_as<Views::GameDetailsPage>()) {
        // wired when the page is created; see NavigateTo
    }
}

void MainWindow::NavigateTo(hstring const& page) {
    currentPage_ = page;

    if (page == L"home") {
        ContentFrame().Navigate(xaml_typename<Views::HomePage>());
        HeaderTitle().Text(L"Home");
        SelectNavButton(HomeNavButton());
    } else if (page == L"library") {
        ContentFrame().Navigate(xaml_typename<Views::LibraryPage>());
        HeaderTitle().Text(L"Library");
        SelectNavButton(LibraryNavButton());
    } else if (page == L"playtime") {
        ContentFrame().Navigate(xaml_typename<Views::PlaytimePage>());
        HeaderTitle().Text(L"Playtime");
        SelectNavButton(PlaytimeNavButton());
    } else if (page == L"settings") {
        ContentFrame().Navigate(xaml_typename<Views::SettingsPage>());
        HeaderTitle().Text(L"Settings");
        SelectNavButton(SettingsNavButton());
    }
}

void MainWindow::OnNavClicked(Windows::Foundation::IInspectable const& sender,
                              Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (auto button = sender.try_as<Microsoft::UI::Xaml::Controls::Button>()) {
        NavigateTo(unbox_value<hstring>(button.Tag()));
    }
}

void MainWindow::OnSearchTextChanged(
    Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
    Microsoft::UI::Xaml::Controls::AutoSuggestBoxTextChangedEventArgs const& args) {
    if (args.Reason() != AutoSuggestionBoxTextChangeReason::UserInput) return;
    if (sender.Text().size() < 2) {
        sender.ItemsSource(nullptr);
        return;
    }

    // Suggest straight from the loaded library so typing never hits the network.
    auto& services = opennow::app::AppServices::Instance();
    auto suggestions = single_threaded_observable_vector<IInspectable>();
    const std::wstring needle = ToLower(sender.Text().c_str());
    for (const auto& game : services.library()) {
        const std::wstring title = ToLower(Utf8ToWide(game.title));
        if (title.find(needle) != std::wstring::npos) {
            suggestions.Append(box_value(Utf8ToWide(game.title)));
        }
        if (suggestions.Size() >= 8) break;
    }
    sender.ItemsSource(suggestions);
}

void MainWindow::OnSearchSubmitted(
    Microsoft::UI::Xaml::Controls::AutoSuggestBox const& sender,
    Microsoft::UI::Xaml::Controls::AutoSuggestBoxQuerySubmittedEventArgs const&) {
    const std::string query = winrt::to_string(sender.Text());
    if (query.empty()) return;
    NavigateTo(L"library");
    if (auto page = ContentFrame().Content().try_as<Views::LibraryPage>()) {
        page.RunSearch(to_hstring(query));
    }
}

void MainWindow::OnAccountClicked(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&) {
    auto& services = opennow::app::AppServices::Instance();
    if (!services.signedIn()) {
        ShowLoginOverlay(true);
        return;
    }

    // A tiny inline menu: sign out or open settings. Kept in code rather than
    // markup because it is the only context menu in the shell.
    Controls::MenuFlyout menu;
    Controls::MenuFlyoutItem settingsItem;
    settingsItem.Text(L"Settings");
    settingsItem.Click([this](auto&&, auto&&) { NavigateTo(L"settings"); });
    Controls::MenuFlyoutItem signOutItem;
    signOutItem.Text(L"Sign out");
    signOutItem.Click([this](auto&&, auto&&) {
        opennow::app::AppServices::Instance().SignOut();
        ShowLoginOverlay(true);
        UpdateAccountUi();
        SetStatus(L"Not signed in", false);
    });
    menu.Items().Append(settingsItem);
    menu.Items().Append(signOutItem);
    menu.ShowAt(AccountButton());
}

void MainWindow::ShowLoginOverlay(bool show) {
    loginOverlayVisible_ = show;
    LoginOverlay().Visibility(show ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::SetStatus(hstring const& text, bool connected) {
    ConnectionText().Text(text);
    ConnectionDot().Fill(connected
                             ? Resources().TryLookup(box_value(L"OpenNowSuccessBrush"))
                                   .as<Microsoft::UI::Xaml::Media::Brush>()
                             : Resources().TryLookup(box_value(L"OpenNowMutedBrush"))
                                   .as<Microsoft::UI::Xaml::Media::Brush>());
}

void MainWindow::UpdateAccountUi() {
    const auto& services = opennow::app::AppServices::Instance();
    if (!services.signedIn()) {
        AccountName().Text(L"Sign in");
        AccountAvatar().DisplayName(L"Guest");
        return;
    }

    const auto profile = services.profile();
    const auto name = profile.displayName.empty()
                          ? (profile.email.empty() ? std::string("Player") : profile.email)
                          : profile.displayName;
    AccountName().Text(to_hstring(name));
    AccountAvatar().DisplayName(to_hstring(name));

    if (!profile.avatarUrl.empty()) {
        AccountAvatar().ProfilePicture(
            Microsoft::UI::Xaml::Media::Imaging::BitmapImage(
                Windows::Foundation::Uri(to_hstring(profile.avatarUrl))));
    }

    const auto subscription = services.subscription();
    if (!subscription.tierName.empty()) {
        SubscriptionChipText().Text(to_hstring(subscription.tierName));
        SubscriptionChip().Visibility(Visibility::Visible);
    }
}

void MainWindow::UpdateHeaderForPage(hstring const& page) {
    if (page == L"home") HeaderTitle().Text(L"Home");
    else if (page == L"library") HeaderTitle().Text(L"Library");
    else if (page == L"playtime") HeaderTitle().Text(L"Playtime");
    else if (page == L"settings") HeaderTitle().Text(L"Settings");
}

Controls::Button MainWindow::ActiveNavButton() {
    if (currentPage_ == L"home") return HomeNavButton();
    if (currentPage_ == L"library") return LibraryNavButton();
    if (currentPage_ == L"playtime") return PlaytimeNavButton();
    return SettingsNavButton();
}

void MainWindow::SelectNavButton(Controls::Button const& button) {
    // WinUI has no built-in selection state on Button, so drive the visual state
    // group declared in Styles.xaml.
    for (auto candidate : {HomeNavButton(), LibraryNavButton(), PlaytimeNavButton(),
                           SettingsNavButton()}) {
        VisualStateManager::GoToState(candidate,
                                      candidate == button ? L"Selected" : L"Unselected", true);
    }
}

}  // namespace winrt::OpenNOW::implementation
