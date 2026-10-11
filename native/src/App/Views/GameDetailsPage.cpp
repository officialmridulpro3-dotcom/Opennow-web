// OpenNOW native client — game details page.

#include "pch.h"

#include "GameDetailsPage.h"
#include "GameDetailsPage.g.cpp"

#include "StreamPage.h"

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "Services/AppServices.h"
#include <opennow/core/Logging.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;
using namespace winrt::Microsoft::UI::Xaml::Navigation;

namespace winrt::OpenNOW::Views::implementation {

GameDetailsPage::GameDetailsPage() { InitializeComponent(); }

void GameDetailsPage::OnNavigatedTo(NavigationEventArgs const& args) {
    if (auto id = args.Parameter().try_as<hstring>()) {
        LoadAsync(*id);
    }
}

winrt::fire_and_forget GameDetailsPage::LoadAsync(hstring gameId) {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();

    co_await winrt::resume_background();

    // Prefer the cached library entry: it already carries ownership, which the
    // details endpoint does not always include.
    std::optional<opennow::core::Game> game;
    const std::string id = winrt::to_string(gameId);
    for (const auto& candidate : services.library()) {
        if (candidate.id == id) {
            game = candidate;
            break;
        }
    }
    if (!game) {
        const auto loaded = services.LoadGameDetails(id);
        if (loaded.ok()) game = *loaded;
    }

    co_await winrt::resume_foreground(DispatcherQueue());

    if (!game) {
        LoadingRing().IsActive(false);
        co_return;
    }

    game_ = game;
    TitleText().Text(to_hstring(game->title));
    DescriptionText().Text(to_hstring(game->description.value_or("")));
    StoreText().Text(to_hstring(game->platformLabel));
    PublisherText().Text(to_hstring(game->publisher));
    DeveloperText().Text(to_hstring(game->developer));

    std::string genres;
    for (const auto& genre : game->genres) {
        if (!genres.empty()) genres += ", ";
        genres += genre;
    }
    GenresText().Text(to_hstring(genres));
    ControllerText().Text(to_hstring(game->controllerSupport ? "Yes" : "Keyboard and mouse"));

    if (!game->heroUrl.empty()) {
        HeroImage().Source(BitmapImage(Windows::Foundation::Uri(to_hstring(game->heroUrl))));
    }

    PopulateMeta();

    LoadingRing().IsActive(false);
    LoadingRing().Visibility(Visibility::Collapsed);
    DetailsScroller().Visibility(Visibility::Visible);
}

void GameDetailsPage::PopulateMeta() {
    if (!game_) return;
    MetaRow().Children().Clear();

    auto chip = [this](const std::string& text) {
        Border border;
        border.Style(Resources()
                         .TryLookup(box_value(L"OpenNowChip"))
                         .as<Microsoft::UI::Xaml::Style>());
        TextBlock block;
        block.Text(to_hstring(text));
        block.Style(Resources()
                        .TryLookup(box_value(L"OpenNowChipText"))
                        .as<Microsoft::UI::Xaml::Style>());
        border.Child(block);
        return border;
    };

    MetaRow().Children().Append(chip(game_->platformLabel));
    if (game_->owned) MetaRow().Children().Append(chip("In your library"));
    if (game_->controllerSupport) MetaRow().Children().Append(chip("Controller"));
}

void GameDetailsPage::OnBack(Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (Frame().CanGoBack()) Frame().GoBack();
}

void GameDetailsPage::OnPlay(Windows::Foundation::IInspectable const&,
                             Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (!game_) return;
    if (Frame() != nullptr) {
        Frame().Navigate(xaml_typename<StreamPage>(),
                         box_value(to_hstring(game_->id + "\n" + game_->title)));
    }
}

void GameDetailsPage::OnPickRegion(Windows::Foundation::IInspectable const&,
                                   Microsoft::UI::Xaml::RoutedEventArgs const&) {
    RegionOverlay().Visibility(Visibility::Visible);
    MeasureRegionsAsync();
}

winrt::fire_and_forget GameDetailsPage::MeasureRegionsAsync() {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();

    co_await winrt::resume_background();
    std::vector<opennow::core::Region> regions = services.regions();
    if (regions.empty()) {
        services.RefreshRegions();
        regions = services.regions();
    }

    // Measure in parallel, then keep the results in the same order.
    std::vector<std::thread> workers;
    std::mutex mutex;
    for (auto& region : regions) {
        workers.emplace_back([&services, &region, &mutex] {
            const auto latency = services.LatencyFor(region);
            std::lock_guard<std::mutex> lock(mutex);
            region.latencyMs = latency;
            region.available = latency.has_value();
        });
    }
    for (auto& worker : workers) worker.join();

    co_await winrt::resume_foreground(DispatcherQueue());

    regions_ = std::move(regions);
    RegionList().Items().Clear();
    for (const auto& region : regions_) {
        const std::string latency = region.latencyMs.has_value()
                                        ? std::to_string(*region.latencyMs) + " ms"
                                        : "unavailable";
        RegionList().Items().Append(box_value(to_hstring(region.label + "  ·  " + latency +
                                                         "  ·  " + region.id)));
    }
    RegionLoading().IsActive(false);
    RegionLoading().Visibility(Visibility::Collapsed);
}

void GameDetailsPage::OnRegionSelected(
    Windows::Foundation::IInspectable const&,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&) {
    const int32_t index = RegionList().SelectedIndex();
    if (index < 0 || index >= static_cast<int32_t>(regions_.size())) return;

    const auto& region = regions_[static_cast<std::size_t>(index)];
    selectedRegionUrl_ = region.streamingBaseUrl;
    opennow::app::AppServices::Instance().SetPreferredRegion(region.streamingBaseUrl);
    RegionButtonText().Text(to_hstring(region.label));
    RegionOverlay().Visibility(Visibility::Collapsed);
    opennow::core::LogInfo("Details", "Region selected: " + selectedRegionUrl_);
}

void GameDetailsPage::OnCancelRegion(Windows::Foundation::IInspectable const&,
                                     Microsoft::UI::Xaml::RoutedEventArgs const&) {
    RegionOverlay().Visibility(Visibility::Collapsed);
}

}  // namespace winrt::OpenNOW::Views::implementation
