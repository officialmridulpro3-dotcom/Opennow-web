// OpenNOW native client — LCARS catalog (GraphQL).
//
// Port of `src/server/gfn/lcarsGraphql.ts` + `catalogBrowse.ts` + the game
// mapping half of `gameAppMapper.ts`. NVIDIA serves the catalog through
// persisted GraphQL queries: the client only sends the query's sha256 hash, and
// falls back to sending the full query text when the server returns HTTP 400
// (which happens whenever NVIDIA rotates the persisted set).

#pragma once

#include <string>
#include <vector>

#include "opennow/core/Http.hpp"
#include "opennow/core/Json.hpp"
#include "opennow/core/Result.hpp"
#include "opennow/core/Types.hpp"

namespace opennow::core::gfn {

enum class LcarsQuery {
    Marquee,
    Main,
    LibrarySection,
    LibrarySectionWithTime,
    FilterGroupAndSortOrderDefinitions,
    AppsWithSearch,
    AppsWithoutSearch,
    AppDataForAppId,
    AddOwnedVariant,
};

struct CatalogPage {
    std::vector<Game> games;
    bool hasNextPage = false;
    std::string endCursor;
    int totalCount = 0;
};

class CatalogService {
public:
    explicit CatalogService(HttpClientPtr http) : http_(std::move(http)) {}

    // Home shelves (MainV2 panel). Each panel section becomes one shelf.
    Result<std::vector<CatalogShelf>> FetchHome(const std::string& token,
                                                const std::string& vpcId,
                                                const std::string& locale = "en_US");

    // Games the account owns, from the Library panel.
    Result<std::vector<Game>> FetchLibrary(const std::string& token, const std::string& vpcId,
                                           const std::string& locale = "en_US");

    // Library + search/browse paging, used by the Library page.
    Result<CatalogPage> BrowseApps(const std::string& token, const std::string& vpcId,
                                   const std::string& searchQuery, const std::string& sortId,
                                   const std::vector<std::string>& filterIds, int fetchCount = 60,
                                   const std::string& cursor = {},
                                   const std::string& locale = "en_US");

    // Single app, used by the game details page.
    Result<Game> FetchApp(const std::string& token, const std::string& appId,
                          const std::string& vpcId, const std::string& locale = "en_US");

    // Marks a store variant as owned (Steam/Epic library linking).
    Result<void> AddOwnedVariant(const std::string& token, const std::string& cmsId,
                                 const std::string& locale = "en_US");

    // v2/serverInfo → vpcId required by every catalog query.
    Result<std::string> FetchVpcId(const std::string& token,
                                   const std::string& providerStreamingBaseUrl = {});

    // ---- pure mapping helpers (unit tested) ---------------------------------------
    static Game GameFromApp(const json::Value& app);
    static std::vector<CatalogShelf> ShelvesFromPanels(const json::Value& payload);
    static std::string OptimizeImage(const std::string& url, int width);

private:
    HttpClientPtr http_;

    Result<json::Value> GraphQl(LcarsQuery query, const json::Value& variables,
                                const std::string& token);
};

}  // namespace opennow::core::gfn
