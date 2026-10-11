// OpenNOW native client — home page.

#include "pch.h"

#include "HomePage.h"
#include "HomePage.g.cpp"

// The view models are declared in HomePage.idl, so cppwinrt writes their
// activation glue next to this page's.
#include "GameViewModel.g.cpp"
#include "ShelfViewModel.g.cpp"

#include "GameDetailsPage.h"

#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include "Services/AppServices.h"
#include <opennow/core/Logging.hpp>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media::Imaging;

namespace {

std::string JoinGenres(const opennow::core::Game& game) {
    std::string joined;
    for (const std::string& genre : game.genres) {
        if (!joined.empty()) joined += ", ";
        joined += genre;
    }
    return joined;
}

}  // namespace

namespace winrt::OpenNOW::implementation {

// ---- view models -----------------------------------------------------------------------------

GameViewModel::GameViewModel(const opennow::core::Game& game)
    : id_(to_hstring(game.id)),
      title_(to_hstring(game.title)),
      platformLabel_(to_hstring(game.platformLabel.empty()
                                    ? std::string(ToString(game.platform))
                                    : game.platformLabel)),
      posterUrl_(to_hstring(game.posterUrl)),
      heroUrl_(to_hstring(game.heroUrl)),
      genres_(to_hstring(JoinGenres(game))),
      owned_(game.owned) {}

hstring GameViewModel::Id() { return id_; }
hstring GameViewModel::Title() { return title_; }
hstring GameViewModel::PlatformLabel() { return platformLabel_; }
hstring GameViewModel::PosterUrl() { return posterUrl_; }
hstring GameViewModel::HeroUrl() { return heroUrl_; }
hstring GameViewModel::Genres() { return genres_; }
bool GameViewModel::Owned() { return owned_; }

hstring ShelfViewModel::Title() { return title_; }
void ShelfViewModel::Title(hstring const& value) { title_ = value; }
hstring ShelfViewModel::CountLabel() { return countLabel_; }
void ShelfViewModel::CountLabel(hstring const& value) { countLabel_ = value; }
Windows::Foundation::Collections::IObservableVector<winrt::OpenNOW::GameViewModel> ShelfViewModel::Games() {
    return games_;
}
void ShelfViewModel::Games(
    Windows::Foundation::Collections::IObservableVector<winrt::OpenNOW::GameViewModel> const& value) {
    games_ = value;
}

// ---- page -------------------------------------------------------------------------------------

HomePage::HomePage() {
    InitializeComponent();
    LoadAsync();
}

winrt::fire_and_forget HomePage::LoadAsync() {
    const auto lifetime = get_strong();
    auto& services = opennow::app::AppServices::Instance();

    co_await winrt::resume_background();
    auto shelves = services.shelves();
    if (shelves.empty()) {
        services.RefreshHome();
        shelves = services.shelves();
    }

    co_await winrt::resume_foreground(DispatcherQueue());

    if (shelves.empty()) {
        LoadingRing().IsActive(false);
        EmptyText().Text(L"Sign in to see your GeForce NOW library.");
        co_return;
    }

    auto shelfModels = single_threaded_observable_vector<winrt::OpenNOW::ShelfViewModel>();
    for (const auto& shelf : shelves) {
        // The first eight cards of every shelf, matching the web client's
        // "mount eight cards per shelf" behaviour on constrained devices.
        auto games = single_threaded_observable_vector<winrt::OpenNOW::GameViewModel>();
        const std::size_t count = std::min<std::size_t>(shelf.games.size(), 8);
        for (std::size_t index = 0; index < count; ++index) {
            games.Append(make<GameViewModel>(shelf.games[index]));
        }

        auto model = make<ShelfViewModel>();
        model.Title(to_hstring(shelf.title));
        model.CountLabel(to_hstring(std::to_string(shelf.games.size()) + " games"));
        model.Games(games);
        shelfModels.Append(model);
    }

    ShelfRepeater().ItemsSource(shelfModels);

    // Spotlight takes the hero artwork of the first game on the first shelf.
    if (!shelves.front().games.empty()) {
        const auto& game = shelves.front().games.front();
        spotlightId_ = game.id;
        if (!game.heroUrl.empty()) {
            SpotlightImage().Source(BitmapImage(Windows::Foundation::Uri(to_hstring(game.heroUrl))));
        }
        SpotlightTitle().Text(to_hstring(game.title));
        std::string meta = game.publisher;
        if (!game.genres.empty()) {
            meta += meta.empty() ? "" : " · ";
            meta += game.genres.front();
        }
        SpotlightMeta().Text(to_hstring(meta));
        Spotlight().Visibility(Visibility::Visible);
    }

    LoadingRing().IsActive(false);
    EmptyState().Visibility(Visibility::Collapsed);
    ShelfScroller().Visibility(Visibility::Visible);
}

void HomePage::RefreshFromServices() { LoadAsync(); }

void HomePage::OnGameCardClicked(Windows::Foundation::IInspectable const& sender,
                                 Microsoft::UI::Xaml::RoutedEventArgs const&) {
    if (auto button = sender.try_as<Button>()) {
        if (auto tag = button.Tag()) {
            OpenDetails(winrt::to_string(unbox_value<hstring>(tag)));
        }
    }
}

void HomePage::OnSpotlightPlay(Windows::Foundation::IInspectable const&,
                               Microsoft::UI::Xaml::RoutedEventArgs const&) {
    OpenDetails(spotlightId_);
}

void HomePage::OnSpotlightDetails(Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&) {
    OpenDetails(spotlightId_);
}

void HomePage::OpenDetails(const std::string& gameId) {
    if (gameId.empty()) return;
    // The shell owns navigation: the page asks its parent frame to move.
    if (auto frame = Frame()) {
        frame.Navigate(xaml_typename<winrt::OpenNOW::GameDetailsPage>(), box_value(to_hstring(gameId)));
    }
}

}  // namespace winrt::OpenNOW::implementation
