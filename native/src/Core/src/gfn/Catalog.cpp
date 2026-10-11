// OpenNOW native client — LCARS catalog implementation.

#include "opennow/core/gfn/Catalog.hpp"

#include <algorithm>
#include <random>
#include <set>

#include "opennow/core/Logging.hpp"
#include "opennow/core/gfn/Constants.hpp"
#include "opennow/core/gfn/GeneratedQueries.inc"

namespace opennow::core::gfn {
namespace {

constexpr std::string_view kGraphQlCdnUrl = "https://games.geforce.com/graphql";
constexpr std::string_view kGraphQlAppsUrl = "https://apps.gxn.nvidia.com/graphql";
constexpr std::string_view kDefaultCloudMatchBaseUrl = "https://prod.cloudmatchbeta.nvidiagrid.net/";

struct QueryDefinition {
    const char* requestType;
    const char* sha256Hash;
};

QueryDefinition DefinitionFor(LcarsQuery query) {
    switch (query) {
        case LcarsQuery::Marquee:
            return {"panels/Marquee",
                    "dd4bddfdef4707dfe340cc2040d6bb9c4c45f706976fca15b2ef33221c385d7f"};
        case LcarsQuery::Main:
            return {"panels/MainV2",
                    "46ec15f267a056e7d5e46e629efa929529e5e7542a4850faece90b9f8fa5f810"};
        case LcarsQuery::LibrarySection:
            return {"panels/Library",
                    "46ec15f267a056e7d5e46e629efa929529e5e7542a4850faece90b9f8fa5f810"};
        case LcarsQuery::LibrarySectionWithTime:
            return {"panels/Library",
                    "7f54d6bbbf3b1c09d0e5264dfa36f0f4aaf5e2678f2089f0cbf0d4dda18c3af9"};
        case LcarsQuery::FilterGroupAndSortOrderDefinitions:
            return {"filterGroupAndSortOrderDefinitions",
                    "ef725de5e93b093de1ac7418fed0ffb4f6ae2b9c14f743ab274a791521488eb9"};
        case LcarsQuery::AppsWithSearch:
            return {"apps", "ea1b5e417c95ceb5c7d6a65aa4613a417ed80b1a8d6a8c26b6953846da1fc513"};
        case LcarsQuery::AppsWithoutSearch:
            return {"apps", "5ae1cfe2e04debdcd81279b5559313abab7d9cfa3ac9d9c048e969b3d445dcb9"};
        case LcarsQuery::AppDataForAppId:
            return {"appMetaData",
                    "cf8b620dfd03617017ba7c858cee65197e1ace5180e41be194b39227227ced63"};
        case LcarsQuery::AddOwnedVariant:
            return {nullptr, "02b373dd20366da6a7184c16a8a84505cc3d15e9f35788c0104c4d16456bcfaf"};
    }
    return {nullptr, nullptr};
}

std::string RandomHuId() {
    static std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> distribution;
    const std::uint64_t value = distribution(engine);
    const std::uint64_t now =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%llx%llx", static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>(value));
    return std::string(buffer);
}

// Maps our query enum onto the names baked into the generated query table.
const char* QueryNameFor(LcarsQuery query) {
    switch (query) {
        case LcarsQuery::Marquee: return "Marquee";
        case LcarsQuery::Main:
        case LcarsQuery::LibrarySection: return "GameSection";
        case LcarsQuery::LibrarySectionWithTime: return "LibrarySectionWithTime";
        case LcarsQuery::FilterGroupAndSortOrderDefinitions: return "FilterGroupAndSort";
        case LcarsQuery::AppDataForAppId: return "AppDataForAppId";
        case LcarsQuery::AddOwnedVariant: return "AddOwnedVariant";
        default: return "";
    }
}

// Empty for the apps queries: NVIDIA has never returned 400 for those, and the
// query text is generated per request with field selections of our own.
std::string FullQueryText(LcarsQuery query) {
    const char* text = ::FullQueryText(QueryNameFor(query));
    return text != nullptr ? std::string(text) : std::string();
}

bool IsNumericId(const std::string& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

const char* const kLandscapeImageKeys[] = {"MARQUEE_HERO_IMAGE", "HERO_IMAGE", "TV_BANNER",
                                           "FEATURE_IMAGE",      "KEY_IMAGE",  "KEY_ART"};
const char* const kPosterImageKeys[] = {"GAME_BOX_ART", "KEY_IMAGE", "KEY_ART"};

std::vector<std::string> NormalizeImageValues(const json::Value* value, int width) {
    std::vector<std::string> out;
    if (value == nullptr) return out;
    auto push = [&](const json::Value& entry) {
        const std::string url = entry.AsString();
        if (url.empty()) return;
        const std::string optimized = CatalogService::OptimizeImage(url, width);
        if (std::find(out.begin(), out.end(), optimized) == out.end()) {
            out.push_back(optimized);
        }
    };
    if (value->isArray()) {
        for (const json::Value& entry : value->Items()) push(entry);
    } else {
        push(*value);
    }
    return out;
}

std::string FirstImage(const json::Value* images, const char* const* keys, std::size_t keyCount,
                       int width) {
    if (images == nullptr) return {};
    for (std::size_t i = 0; i < keyCount; ++i) {
        const std::vector<std::string> values =
            NormalizeImageValues(images->Find(keys[i]), width);
        if (!values.empty()) return values.front();
    }
    return {};
}

}  // namespace

std::string CatalogService::OptimizeImage(const std::string& url, int width) {
    // NVIDIA's CDN accepts a width template and a query parameter. Asking for
    // the size we actually render keeps a 1080p shelf from downloading 4K art.
    const std::size_t templatePosition = url.find("{width}");
    if (templatePosition != std::string::npos) {
        std::string out = url;
        out.replace(templatePosition, 7, std::to_string(width));
        return out;
    }
    if (url.find('?') == std::string::npos) {
        return url + "?w=" + std::to_string(width);
    }
    if (url.find("w=") != std::string::npos) return url;
    return url + "&w=" + std::to_string(width);
}

Game CatalogService::GameFromApp(const json::Value& app) {
    Game game;
    game.id = app.Find("id")->AsString();
    game.title = app.Find("title")->AsString();

    const json::Value* images = app.Find("images");
    game.heroUrl = FirstImage(images, kLandscapeImageKeys,
                              sizeof(kLandscapeImageKeys) / sizeof(kLandscapeImageKeys[0]), 1200);
    game.posterUrl = FirstImage(images, kPosterImageKeys,
                                sizeof(kPosterImageKeys) / sizeof(kPosterImageKeys[0]), 900);
    if (game.posterUrl.empty()) game.posterUrl = game.heroUrl;
    if (game.heroUrl.empty()) game.heroUrl = game.posterUrl;
    if (const json::Value* logo = images ? images->Find("GAME_LOGO") : nullptr) {
        const std::vector<std::string> logos = NormalizeImageValues(logo, 600);
        if (!logos.empty()) game.logoUrl = logos.front();
    }

    game.developer = app.Find("developerName")->AsString();
    game.publisher = app.Find("publisherName")->AsString();
    if (const json::Value* description = app.Find("description")) {
        const std::string text = description->AsString();
        if (!text.empty()) game.description = text;
    }

    if (const json::Value* genres = app.Find("genres")) {
        for (const json::Value& genre : genres->Items()) {
            const std::string value = genre.isString() ? genre.AsString()
                                                       : genre.Find("name")->AsString();
            if (!value.empty()) game.genres.push_back(value);
        }
    }

    // Variants carry the store, the launch id, ownership and controller flags.
    std::set<std::string> stores;
    std::string bestLaunchId;
    bool anyOwned = false;
    bool anyController = false;

    if (const json::Value* variants = app.Find("variants")) {
        for (const json::Value& variant : variants->Items()) {
            const std::string store = variant.Find("appStore")->AsString();
            if (!store.empty()) stores.insert(store);
            const std::string variantId = variant.Find("id")->AsString();
            if (bestLaunchId.empty() && !variantId.empty()) bestLaunchId = variantId;

            if (const json::Value* controls = variant.Find("supportedControls")) {
                auto markController = [&](const json::Value& entry) {
                    const std::string value = entry.AsString();
                    for (char& c : const_cast<std::string&>(value)) {
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    }
                    if (value.find("gamepad") != std::string::npos ||
                        value.find("controller") != std::string::npos) {
                        anyController = true;
                    }
                };
                if (controls->isArray()) {
                    for (const json::Value& entry : controls->Items()) markController(entry);
                } else {
                    markController(*controls);
                }
            }

            if (const json::Value* gfn = variant.Find("gfn")) {
                if (const json::Value* library = gfn->Find("library")) {
                    const std::string status = library->Find("status")->AsString();
                    if (library->Find("selected")->AsBool(false) ||
                        status == "OWNED" || status == "IN_LIBRARY" || status == "owned") {
                        anyOwned = true;
                    }
                }
                if (const json::Value* status = gfn->Find("status")) {
                    const std::string value = status->AsString();
                    if (value == "OWNED" || value == "owned") anyOwned = true;
                }
            }
        }
    }

    if (const json::Value* gfn = app.Find("gfn")) {
        if (const json::Value* library = gfn->Find("library")) {
            if (library->Find("selected")->AsBool(false)) anyOwned = true;
        }
    }

    game.owned = anyOwned || app.Find("isInLibrary")->AsBool(false);
    game.controllerSupport = anyController;
    game.variants.assign(stores.begin(), stores.end());
    if (!stores.empty()) {
        game.platform = ParseStorePlatform(*stores.begin());
        game.platformLabel = std::string(ToString(game.platform));
    }

    // CloudMatch launches the numeric app id when the catalog exposes one.
    game.launchUrl = bestLaunchId;
    if (IsNumericId(game.id)) game.storeAppId = std::strtoll(game.id.c_str(), nullptr, 10);

    if (const json::Value* players = app.Find("maxOnlinePlayers")) {
        game.playerCount = players->AsInt(0);
    }
    return game;
}

std::vector<CatalogShelf> CatalogService::ShelvesFromPanels(const json::Value& payload) {
    std::vector<CatalogShelf> shelves;
    const json::Value* data = payload.Find("data");
    if (data == nullptr) return shelves;
    const json::Value* panels = data->Find("panels");
    if (panels == nullptr) return shelves;

    for (const json::Value& panel : panels->Items()) {
        const std::string panelId = panel.Find("id")->AsString(panel.Find("name")->AsString());
        const json::Value* sections = panel.Find("sections");
        if (sections == nullptr) continue;
        for (const json::Value& section : sections->Items()) {
            CatalogShelf shelf;
            shelf.id = section.Find("id")->AsString(section.Find("title")->AsString());
            shelf.title = section.Find("title")->AsString();
            shelf.titleKey = shelf.title;

            const json::Value* items = section.Find("items");
            if (items == nullptr) continue;
            for (const json::Value& item : items->Items()) {
                if (item.Find("__typename")->AsString() != "GameItem") continue;
                const json::Value* app = item.Find("app");
                if (app == nullptr) continue;
                Game game = GameFromApp(*app);
                if (game.id.empty() || game.title.empty()) continue;
                shelf.games.push_back(std::move(game));
            }
            if (!shelf.games.empty()) shelves.push_back(std::move(shelf));
        }
    }
    return shelves;
}

Result<json::Value> CatalogService::GraphQl(LcarsQuery query, const json::Value& variables,
                                            const std::string& token) {
    const QueryDefinition definition = DefinitionFor(query);
    if (definition.sha256Hash == nullptr) {
        return Result<json::Value>::Fail("LCARS query has no persisted hash");
    }

    json::Value extensions = json::ObjectValue();
    json::Value persisted = json::ObjectValue();
    persisted.Set("sha256Hash", std::string(definition.sha256Hash));
    extensions.Set("persistedQuery", std::move(persisted));

    const std::string url =
        std::string(kGraphQlCdnUrl) + "?extensions=" + UrlEncode(extensions.Dump()) +
        "&huId=" + UrlEncode(RandomHuId()) + "&variables=" + UrlEncode(variables.Dump()) +
        (definition.requestType != nullptr
             ? "&requestType=" + UrlEncode(definition.requestType)
             : "");

    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers = BuildLcarsHeaders(token, kLcarsClientId);
    request.headers.push_back(HttpHeader{"Content-Type", "application/graphql"});

    HttpResponse response = http_->Send(request);

    // HTTP 400 means NVIDIA no longer knows the persisted hash. Re-sending the
    // full query text is the documented recovery path, so keep the query
    // strings alongside the hashes in the same table.
    if (response.status == 400) {
        LogWarn("Catalog", "Persisted query rejected; retrying with inline query text.");
        const std::string fallbackQuery = FullQueryText(query);
        if (!fallbackQuery.empty()) {
            const std::string retryUrl = url + "&query=" + UrlEncode(fallbackQuery);
            request.url = retryUrl;
            response = http_->Send(request);
        }
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) {
        return Result<json::Value>::Fail("Catalog response was not valid JSON (HTTP " +
                                         std::to_string(response.status) + ")");
    }
    if (!response.ok()) {
        return Result<json::Value>::Fail("Catalog request failed (HTTP " +
                                         std::to_string(response.status) + ")");
    }
    if (const json::Value* errors = parsed.value.Find("errors")) {
        std::string message;
        for (const json::Value& error : errors->Items()) {
            if (!message.empty()) message += ", ";
            message += error.Find("message")->AsString("Unknown GraphQL error");
        }
        if (!message.empty()) return Result<json::Value>::Fail(message);
    }
    return Result<json::Value>::Ok(parsed.value);
}

Result<std::vector<CatalogShelf>> CatalogService::FetchHome(const std::string& token,
                                                            const std::string& vpcId,
                                                            const std::string& locale) {
    json::Value variables = json::ObjectValue();
    variables.Set("vpcId", vpcId);
    variables.Set("locale", locale);
    json::Value panelNames = json::ArrayValue();
    panelNames.Push(json::Value("Main"));
    variables.Set("panelNames", std::move(panelNames));

    const Result<json::Value> payload = GraphQl(LcarsQuery::Main, variables, token);
    if (!payload.ok()) return Result<std::vector<CatalogShelf>>::Fail(payload.error);

    std::vector<CatalogShelf> shelves = ShelvesFromPanels(*payload);
    return Result<std::vector<CatalogShelf>>::Ok(std::move(shelves));
}

Result<std::vector<Game>> CatalogService::FetchLibrary(const std::string& token,
                                                       const std::string& vpcId,
                                                       const std::string& locale) {
    json::Value variables = json::ObjectValue();
    variables.Set("vpcId", vpcId);
    variables.Set("locale", locale);
    json::Value panelNames = json::ArrayValue();
    panelNames.Push(json::Value("Library"));
    variables.Set("panelNames", std::move(panelNames));

    const Result<json::Value> payload =
        GraphQl(LcarsQuery::LibrarySectionWithTime, variables, token);
    if (!payload.ok()) return Result<std::vector<Game>>::Fail(payload.error);

    std::vector<Game> games;
    std::set<std::string> seen;
    for (CatalogShelf& shelf : ShelvesFromPanels(*payload)) {
        for (Game& game : shelf.games) {
            if (seen.insert(game.id).second) games.push_back(std::move(game));
        }
    }
    for (Game& game : games) game.owned = true;
    return Result<std::vector<Game>>::Ok(std::move(games));
}

Result<CatalogPage> CatalogService::BrowseApps(const std::string& token,
                                               const std::string& vpcId,
                                               const std::string& searchQuery,
                                               const std::string& sortId,
                                               const std::vector<std::string>& filterIds,
                                               int fetchCount, const std::string& cursor,
                                               const std::string& locale) {
    const bool searching = !searchQuery.empty();
    const LcarsQuery query =
        searching ? LcarsQuery::AppsWithSearch : LcarsQuery::AppsWithoutSearch;

    json::Value variables = json::ObjectValue();
    variables.Set("vpcId", vpcId);
    variables.Set("locale", locale);
    variables.Set("first", fetchCount < 24 ? 24 : (fetchCount > 200 ? 200 : fetchCount));
    if (!cursor.empty()) variables.Set("after", cursor);
    if (searching) variables.Set("searchQuery", searchQuery);
    if (!sortId.empty()) variables.Set("sortOrderId", sortId);
    if (!filterIds.empty()) {
        json::Value filters = json::ArrayValue();
        for (const std::string& filter : filterIds) filters.Push(json::Value(filter));
        variables.Set("filterIds", std::move(filters));
    }

    const Result<json::Value> payload = GraphQl(query, variables, token);
    if (!payload.ok()) return Result<CatalogPage>::Fail(payload.error);

    CatalogPage page;
    if (const json::Value* data = payload->Find("data")) {
        if (const json::Value* apps = data->Find("apps")) {
            if (const json::Value* items = apps->Find("items")) {
                for (const json::Value& item : items->Items()) {
                    page.games.push_back(GameFromApp(item));
                }
            }
            if (const json::Value* pageInfo = apps->Find("pageInfo")) {
                page.hasNextPage = pageInfo->Find("hasNextPage")->AsBool(false);
                page.endCursor = pageInfo->Find("endCursor")->AsString();
                page.totalCount = static_cast<int>(pageInfo->Find("totalCount")->AsInt(0));
            }
        }
    }
    return Result<CatalogPage>::Ok(std::move(page));
}

Result<Game> CatalogService::FetchApp(const std::string& token, const std::string& appId,
                                      const std::string& vpcId, const std::string& locale) {
    json::Value variables = json::ObjectValue();
    variables.Set("vpcId", vpcId);
    variables.Set("locale", locale);
    variables.Set("appId", appId);

    const Result<json::Value> payload = GraphQl(LcarsQuery::AppDataForAppId, variables, token);
    if (!payload.ok()) return Result<Game>::Fail(payload.error);

    if (const json::Value* data = payload->Find("data")) {
        if (const json::Value* app = data->Find("app")) return Result<Game>::Ok(GameFromApp(*app));
        if (const json::Value* appMetaData = data->Find("appMetaData")) {
            if (const json::Value* app = appMetaData->Find("app")) {
                return Result<Game>::Ok(GameFromApp(*app));
            }
        }
    }
    return Result<Game>::Fail("Catalog response did not contain the app");
}

Result<void> CatalogService::AddOwnedVariant(const std::string& token, const std::string& cmsId,
                                             const std::string& locale) {
    json::Value variables = json::ObjectValue();
    variables.Set("locale", locale);
    variables.Set("cmsId", cmsId);
    const Result<json::Value> payload = GraphQl(LcarsQuery::AddOwnedVariant, variables, token);
    if (!payload.ok()) return Result<void>::Fail(payload.error);
    return Result<void>::Ok();
}

Result<std::string> CatalogService::FetchVpcId(const std::string& token,
                                               const std::string& providerStreamingBaseUrl) {
    // Only NVIDIA's own hosts are trusted for this call; anything else falls
    // back to the known-good default so a bad provider URL cannot leak the
    // bearer token to a third party.
    std::string base = providerStreamingBaseUrl.empty() ? std::string(kDefaultCloudMatchBaseUrl)
                                                        : providerStreamingBaseUrl;
    std::string host;
    const std::size_t schemeEnd = base.find("://");
    if (schemeEnd != std::string::npos) {
        const std::size_t hostStart = schemeEnd + 3;
        const std::size_t hostEnd = base.find_first_of("/:", hostStart);
        host = base.substr(hostStart, hostEnd == std::string::npos ? std::string::npos
                                                                  : hostEnd - hostStart);
    }
    std::string lowered = host;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const std::string kRegionSuffix = ".geforcenow.nvidiagrid.net";
    const bool trustedHost =
        lowered == "prod.cloudmatchbeta.nvidiagrid.net" || lowered == "img.nvidiagrid.net" ||
        (lowered.size() >= kRegionSuffix.size() &&
         lowered.compare(lowered.size() - kRegionSuffix.size(), kRegionSuffix.size(),
                         kRegionSuffix) == 0);
    if (!trustedHost) base = std::string(kDefaultCloudMatchBaseUrl);

    if (base.back() != '/') base.push_back('/');

    HttpRequest request;
    request.method = "GET";
    request.url = base + "v2/serverInfo";
    request.headers = BuildLcarsHeaders(token, kLcarsClientId);

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<std::string>::Fail("serverInfo request failed (HTTP " +
                                         std::to_string(response.status) + ")");
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) return Result<std::string>::Fail("serverInfo response was not valid JSON");

    const std::string vpcId = parsed.value.Find("vpcId")->AsString();
    if (vpcId.empty()) {
        return Result<std::string>::Fail("serverInfo response did not include a vpcId");
    }
    return Result<std::string>::Ok(vpcId);
}

}  // namespace opennow::core::gfn
