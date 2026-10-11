// OpenNOW native client — membership subscription and streaming regions.

#pragma once

#include <string>
#include <vector>

#include "opennow/core/Http.hpp"
#include "opennow/core/Result.hpp"
#include "opennow/core/Types.hpp"

namespace opennow::core::gfn {

class SubscriptionService {
public:
    explicit SubscriptionService(HttpClientPtr http) : http_(std::move(http)) {}

    // https://mes.geforcenow.com/v4/subscriptions — tier, session minutes, storage.
    Result<Subscription> FetchSubscription(const std::string& token, const std::string& userId,
                                           const std::string& vpcId = "NP-AMS-08");

    // Regions advertised by the provider; falls back to the known zone list.
    Result<std::vector<Region>> FetchRegions(const std::string& token,
                                             const std::string& streamingBaseUrl);
    std::vector<Region> FallbackRegions() const;

    // Bounded HTTPS latency probe, run in parallel by the caller. The web client
    // measures from the browser; here we measure with the same HTTP stack used
    // for streaming, which is what actually matters.
    Result<int> MeasureLatency(const std::string& streamingBaseUrl);

private:
    HttpClientPtr http_;
};

}  // namespace opennow::core::gfn
