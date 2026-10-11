// OpenNOW native client — subscription and regions.

#include "opennow/core/gfn/Subscription.hpp"

#include <algorithm>
#include <chrono>

#include "opennow/core/Json.hpp"
#include "opennow/core/Logging.hpp"
#include "opennow/core/gfn/Constants.hpp"

namespace opennow::core::gfn {
namespace {

constexpr std::string_view kMesUrl = "https://mes.geforcenow.com/v4/subscriptions";

struct FallbackZone {
    const char* name;
    const char* zone;
};

// Mirrors FALLBACK_REGION_ZONES in the web server: NVIDIA's own dynamic list is
// authoritative when it answers, but a stale-but-usable list beats an empty
// region picker.
constexpr FallbackZone kFallbackZones[] = {
    {"US Central - Dallas", "np-dal-08"},
    {"US East - Ashburn", "np-ash-08"},
    {"US Midwest - Chicago", "np-chi-08"},
    {"US Northeast - Newark", "np-nwk-08"},
    {"US Northwest - Seattle", "np-sea-08"},
    {"US South - Atlanta", "np-atl-08"},
    {"US Southeast - Miami", "np-mia-08"},
    {"US Southwest - Los Angeles", "np-lax-08"},
    {"US West - San Jose", "np-sjc-08"},
    {"CA East - Montreal", "np-yul-08"},
    {"EU Northeast - Amsterdam", "np-ams-08"},
    {"EU Central - Frankfurt", "np-frk-08"},
    {"EU Southwest - Paris", "np-par-08"},
    {"EU West - London", "np-lhr-08"},
    {"EU Northwest - Stockholm", "np-sto-08"},
    {"EU Southeast - Sofia", "np-sof-02"},
    {"EU Southeast - Warsaw", "np-waw-02"},
    {"Caucasus South - Yerevan", "np-evn-02"},
    {"Africa South - Johannesburg", "np-jnb-02"},
    {"SA East - Sao Paulo", "np-gru-08"},
    {"LATAM South - Montevideo", "np-mvd-02"},
    {"AU East - Sydney", "np-syd-02"},
    {"AU West - Perth", "np-per-02"},
    {"JP East - Tokyo", "np-nrt-08"},
    {"SG - Singapore", "np-sin-02"},
    {"KR - Seoul", "np-icn-02"},
    {"TR Central - Ankara", "np-ank-02"},
    {"TR West - Istanbul", "np-ist-02"},
    {"TW - Taipei", "np-tpe-02"},
    {"Netherlands North", "np-ams-08"},
    {"Netherlands South", "np-ams-09"},
    {"Germany North", "np-frk-09"},
    {"UK South", "np-lhr-09"},
    {"France Central", "np-par-09"},
};

double ParseMinutes(const json::Value* value) {
    if (value == nullptr) return 0.0;
    return value->AsDouble(0.0);
}

}  // namespace

std::vector<Region> SubscriptionService::FallbackRegions() const {
    std::vector<Region> regions;
    std::vector<std::string> seen;
    for (const FallbackZone& zone : kFallbackZones) {
        const std::string url = std::string("https://") + zone.zone + ".cloudmatchbeta.nvidiagrid.net/";
        if (std::find(seen.begin(), seen.end(), url) != seen.end()) continue;
        seen.push_back(url);
        Region region;
        region.label = zone.name;
        region.id = zone.zone;
        region.streamingBaseUrl = url;
        regions.push_back(std::move(region));
    }
    std::sort(regions.begin(), regions.end(),
              [](const Region& a, const Region& b) { return a.label < b.label; });
    return regions;
}

Result<Subscription> SubscriptionService::FetchSubscription(const std::string& token,
                                                            const std::string& userId,
                                                            const std::string& vpcId) {
    const std::string url =
        std::string(kMesUrl) + "?serviceName=gfn_pc&languageCode=en_US&vpcId=" + UrlEncode(vpcId) +
        "&userId=" + UrlEncode(userId);

    HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.headers = BuildLcarsHeaders(token, kLcarsClientId);

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        return Result<Subscription>::Fail("Subscription request failed (HTTP " +
                                          std::to_string(response.status) + ")");
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) return Result<Subscription>::Fail("Subscription response was not valid JSON");

    Subscription subscription;
    subscription.tierName = parsed.value.Find("membershipTier")->AsString("FREE");
    subscription.tierId = subscription.tierName;
    subscription.active = !subscription.tierName.empty();

    const double allotted = ParseMinutes(parsed.value.Find("allottedTimeInMinutes"));
    const double purchased = ParseMinutes(parsed.value.Find("purchasedTimeInMinutes"));
    const double rolledOver = ParseMinutes(parsed.value.Find("rolledOverTimeInMinutes"));
    const double fallbackTotal = allotted + purchased + rolledOver;
    const double total = parsed.value.Find("totalTimeInMinutes")
                             ? ParseMinutes(parsed.value.Find("totalTimeInMinutes"))
                             : fallbackTotal;
    const double remaining = ParseMinutes(parsed.value.Find("remainingTimeInMinutes"));

    subscription.maxSessionMinutes = static_cast<std::int64_t>(total);
    subscription.sessionTimeMinutes = static_cast<std::int64_t>(total - remaining);

    if (const json::Value* subType = parsed.value.Find("subType")) {
        if (subType->AsString() == "UNLIMITED") subscription.maxSessionMinutes = 0;  // unlimited
    }
    if (const json::Value* renewal = parsed.value.Find("renewalDate")) {
        const std::string value = renewal->AsString();
        if (!value.empty()) subscription.renewalDate = value;
    }
    return Result<Subscription>::Ok(subscription);
}

Result<std::vector<Region>> SubscriptionService::FetchRegions(const std::string& token,
                                                              const std::string& streamingBaseUrl) {
    std::string base = streamingBaseUrl.empty() ? std::string(kDefaultStreamingServiceUrl)
                                                : streamingBaseUrl;
    if (base.back() != '/') base.push_back('/');

    HttpRequest request;
    request.method = "GET";
    request.url = base + "v2/regions";
    request.headers = BuildLcarsHeaders(token, kLcarsClientId);

    const HttpResponse response = http_->Send(request);
    if (!response.ok()) {
        LogWarn("Regions", "Dynamic region list unavailable; using the built-in zone table.");
        return Result<std::vector<Region>>::Ok(FallbackRegions());
    }

    const json::ParseResult parsed = json::Value::Parse(response.body);
    if (!parsed.ok) return Result<std::vector<Region>>::Ok(FallbackRegions());

    std::vector<Region> regions;
    const json::Value* list = parsed.value.Find("regions");
    if (list == nullptr) list = parsed.value.Find("data");
    if (list != nullptr && list->isArray()) {
        for (const json::Value& entry : list->Items()) {
            const std::string url = entry.Find("url")->AsString(entry.Find("streamingUrl")->AsString());
            if (url.empty()) continue;
            Region region;
            region.label = entry.Find("name")->AsString(entry.Find("zone")->AsString());
            region.id = entry.Find("zone")->AsString(region.label);
            region.streamingBaseUrl = url;
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty()) regions = FallbackRegions();
    return Result<std::vector<Region>>::Ok(std::move(regions));
}

Result<int> SubscriptionService::MeasureLatency(const std::string& streamingBaseUrl) {
    std::string base = streamingBaseUrl;
    if (base.back() != '/') base.push_back('/');

    HttpRequest request;
    request.method = "GET";
    request.url = base + "v2/serverInfo";
    request.timeout = std::chrono::milliseconds(4000);  // bounded, like the web client
    request.headers = BuildLcarsHeaders({}, kLcarsClientId);

    const auto started = std::chrono::steady_clock::now();
    const HttpResponse response = http_->Send(request);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    if (!response.ok()) return Result<int>::Fail("Region did not answer the latency probe");
    return Result<int>::Ok(static_cast<int>(elapsed));
}

}  // namespace opennow::core::gfn
