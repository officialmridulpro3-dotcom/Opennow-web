// OpenNOW native client — GFN header builders.

#include "opennow/core/gfn/Constants.hpp"

namespace opennow::core::gfn {

std::vector<HttpHeader> BuildAuthHeaders(std::string_view clientId, std::string_view contentType,
                                         std::string_view deviceId) {
    std::vector<HttpHeader> headers;
    headers.push_back(HttpHeader{"Accept", "application/json, text/plain, */*"});
    headers.push_back(HttpHeader{"User-Agent", std::string(kUserAgent)});
    headers.push_back(HttpHeader{"Origin", std::string(kNvidiaFileOrigin)});
    headers.push_back(HttpHeader{"Referer", std::string(kNvidiaFileReferer)});
    headers.push_back(HttpHeader{"nv-client-id", std::string(clientId)});
    if (!contentType.empty()) {
        headers.push_back(HttpHeader{"Content-Type", std::string(contentType)});
    }
    if (!deviceId.empty()) {
        headers.push_back(HttpHeader{"x-device-id", std::string(deviceId)});
    }
    return headers;
}

std::vector<HttpHeader> BuildLcarsHeaders(std::string_view token, std::string_view clientId,
                                          std::string_view contentType) {
    std::vector<HttpHeader> headers;
    if (!token.empty()) {
        headers.push_back(HttpHeader{"Authorization", "Bearer " + std::string(token)});
    }
    if (!contentType.empty()) {
        headers.push_back(HttpHeader{"Content-Type", std::string(contentType)});
    }
    headers.push_back(HttpHeader{"Origin", std::string(kGfnPlayOrigin)});
    headers.push_back(HttpHeader{"Referer", std::string(kGfnPlayReferer)});
    headers.push_back(HttpHeader{"Accept", "application/json, text/plain, */*"});
    headers.push_back(HttpHeader{"User-Agent", std::string(kUserAgent)});
    headers.push_back(HttpHeader{"x-nv-client-id", std::string(clientId)});
    headers.push_back(HttpHeader{"nv-client-id", std::string(clientId)});
    headers.push_back(HttpHeader{"nv-client-type", "NATIVE"});
    headers.push_back(HttpHeader{"nv-client-streamer", "NVIDIA-CLASSIC"});
    headers.push_back(HttpHeader{"nv-device-os", "WINDOWS"});
    headers.push_back(HttpHeader{"nv-client-version", std::string(kClientVersion)});
    return headers;
}

std::vector<HttpHeader> BuildCloudMatchHeaders(std::string_view token, std::string_view clientId,
                                               std::string_view deviceId, bool includeOrigin) {
    std::vector<HttpHeader> headers;
    if (!token.empty()) {
        headers.push_back(HttpHeader{"Authorization", "Bearer " + std::string(token)});
    }
    headers.push_back(HttpHeader{"Content-Type", "application/json"});
    headers.push_back(HttpHeader{"Accept", "application/json"});
    if (includeOrigin) {
        headers.push_back(HttpHeader{"Origin", std::string(kGfnPlayOrigin)});
        headers.push_back(HttpHeader{"Referer", std::string(kGfnPlayReferer)});
    }
    headers.push_back(HttpHeader{"User-Agent", std::string(kUserAgent)});
    headers.push_back(HttpHeader{"nv-client-id", std::string(clientId)});
    headers.push_back(HttpHeader{"nv-client-type", "NATIVE"});
    headers.push_back(HttpHeader{"nv-client-streamer", "NVIDIA-CLASSIC"});
    headers.push_back(HttpHeader{"nv-device-os", "WINDOWS"});
    headers.push_back(HttpHeader{"nv-client-version", std::string(kClientVersion)});
    if (!deviceId.empty()) {
        headers.push_back(HttpHeader{"nv-device-hash-id", std::string(deviceId)});
        headers.push_back(HttpHeader{"x-device-id", std::string(deviceId)});
    }
    return headers;
}

}  // namespace opennow::core::gfn
