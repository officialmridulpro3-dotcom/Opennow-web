// OpenNOW native client — library page.

#include "pch.h"

#include "LibraryPage.h"
#include "LibraryPage.g.cpp"

#include "GameDetailsPage.h"

#include <algorithm>

#include "Services/AppServices.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::OpenNOW::implementation {

LibraryPage::LibraryPage() {
    InitializeComponent();
    LoadAsync();
}

winrt::fire_and_forget LibraryPage::LoadAsync() {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();

    co_await winrt::resume_background();
    std::vector<opennow::core::Game> games = services.library();
    if (games.empty()) {
        services.RefreshLibrary();
        games = services.library();
    }

    co_await winrt::resume_foreground(DispatcherQueue());

    games_ = std::move(games);
    LoadingRing().IsActive(false);
    LoadingRing().Visibility(Visibility::Collapsed);
    ApplyFilters();
}

void LibraryPage::RunSearch(hstring const& query) {
    activeQuery_ = winrt::to_string(query);

    auto& services = opennow::app::AppServices::Instance();
    if (activeQuery_.size() < 2) {
        ApplyFilters();
        return;
    }

    // Search hits the catalog; the library view then filters the results locally
    // so switching sort order never re-queries.
    LoadingRing().IsActive(true);
    LoadingRing().Visibility(Visibility::Visible);

    const auto lifetime = get_strong();
    std::string queryCopy = activeQuery_;
    [this, lifetime, queryCopy]() -> winrt::fire_and_forget {
        auto& services = opennow::app::AppServices::Instance();
        co_await winrt::resume_background();
        const auto results = services.Search(queryCopy);
        co_await winrt::resume_foreground(DispatcherQueue());
        if (results.ok()) {
            games_ = *results;
            ApplyFilters();
        }
        LoadingRing().IsActive(false);
        LoadingRing().Visibility(Visibility::Collapsed);
    }();
}

void LibraryPage::ApplyFilters() {
    std::vector<opennow::core::Game> filtered;
    const bool ownedOnly = OwnedToggle().IsChecked().GetBoolean();
    for (const auto& game : games_) {
        if (ownedOnly && !game.owned) continue;
        filtered.push_back(game);
    }

    switch (SortBox().SelectedIndex()) {
        case 1:  // A → Z
            std::sort(filtered.begin(), filtered.end(),
                      [](const opennow::core::Game& a, const opennow::core::Game& b) {
                          return a.title < b.title;
                      });
            break;
        case 2:  // Most played
            std::sort(filtered.begin(), filtered.end(),
                      [](const opennow::core::Game& a, const opennow::core::Game& b) {
                          return a.playerCount > b.playerCount;
                      });
            break;
        default:  // Recently played: the catalog order is already that order
            break;
    }

    auto models = single_threaded_observable_vector<GameViewModel>();
    for (const auto& game : filtered) {
        models.Append(make<GameViewModel>(game));
    }
    GameGrid().ItemsSource(models);

    ResultCount().Text(to_hstring(std::to_string(filtered.size()) + " games"));
    const bool empty = filtered.empty();
    GameGrid().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);
    EmptyText().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
}

void LibraryPage::OnFilterChanged(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ApplyFilters();
}

void LibraryPage::OnGameClicked(Windows::Foundation::IInspectable const&,
                                Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args) {
    if (auto model = args.ClickedItem().try_as<GameViewModel>()) {
        if (auto frame = Frame()) {
            frame.Navigate(xaml_typename<GameDetailsPage>(), box_value(model.Id()));
        }
    }
}

void LibraryPage::OnContainerChanging(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args) {
    using namespace winrt::Microsoft::UI::Xaml::Media::Animation;

    if (args.InRecycleQueue()) return;

    auto container = args.ItemContainer().try_as<UIElement>();
    if (container == nullptr) return;

    if (args.Phase() == 0) {
        // Phase 0: defer the contents and start from transparent.
        args.Handled(true);
        container.Opacity(0.0);
        args.RegisterUpdateCallback({this, &LibraryPage::OnContainerChanging});
        return;
    }

    if (args.Phase() == 1) {
        Storyboard storyboard;
        DoubleAnimation fade;
        fade.To(1.0);
        fade.Duration(DurationHelper::FromTimeSpan(std::chrono::milliseconds(180)));
        Storyboard::SetTarget(fade, container);
        Storyboard::SetTargetProperty(fade, L"Opacity");
        storyboard.Children().Append(fade);
        storyboard.Begin();
    }
}

}  // namespace winrt::OpenNOW::implementation
