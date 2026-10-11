// OpenNOW native client — signaling message layer.

#include "opennow/core/gfn/Signaling.hpp"

#include <algorithm>
#include <cstring>

#include "opennow/core/Logging.hpp"

namespace opennow::core::gfn {
namespace {

bool StartsWithFold(const std::string& text, const char* prefix) {
    const std::size_t length = std::strlen(prefix);
    if (text.size() < length) return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string ExtractHostFromUrl(const std::string& url) {
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    const std::size_t hostStart = scheme + 3;
    const std::size_t hostEnd = url.find_first_of("/?", hostStart);
    return url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos
                                                             : hostEnd - hostStart);
}

std::string BuildSignalingUrl(const std::string& resourcePath, const std::string& serverIp,
                              std::string* hostOut) {
    std::string host = serverIp;
    std::string path = resourcePath.empty() ? "/nvst/" : resourcePath;

    if (StartsWithFold(path, "wss://") || StartsWithFold(path, "ws://")) {
        host = ExtractHostFromUrl(path);
        return path;
    }

    if (StartsWithFold(path, "rtsps://") || StartsWithFold(path, "rtsp://")) {
        // rtsps://host:port/path -> wss://host/nvst/
        const std::string extracted = ExtractHostFromUrl(path);
        if (!extracted.empty()) {
            const std::size_t colon = extracted.find(':');
            host = colon == std::string::npos ? extracted : extracted.substr(0, colon);
        }
        path = "/nvst/";
    }

    if (!path.empty() && path.front() != '/') path = "/" + path;

    if (hostOut != nullptr) *hostOut = host;
    return "wss://" + host + ":443" + path;
}

SignalingInfo ResolveSignaling(const json::Value& payload) {
    SignalingInfo info;
    const json::Value* session = payload.Find("session");
    if (session == nullptr) return info;

    const json::Value* connections = session->Find("connectionInfo");
    std::string signalingResourcePath;
    std::string fallbackHost;

    if (connections != nullptr) {
        for (const json::Value& connection : connections->Items()) {
            const int usage = static_cast<int>(connection.Find("usage")->AsInt(-1));
            const std::string ip = connection.Find("ip")->AsString();
            const std::string resourcePath = connection.Find("resourcePath")->AsString();
            if (usage == 14 && !ip.empty()) {
                signalingResourcePath = resourcePath;
                fallbackHost = ip;
            }
            if (usage == 14 && !resourcePath.empty() &&
                StartsWithFold(resourcePath, "rtsps://")) {
                info.rtspsEndpoints.push_back(resourcePath);
            }
            if (info.serverIp.empty() && !ip.empty()) info.serverIp = ip;
        }
    }

    // streamingServerIp priority chain, matching the reference client.
    if (const json::Value* direct = session->Find("serverIp")) {
        const std::string value = direct->AsString();
        if (!value.empty()) info.serverIp = value;
    }
    if (info.serverIp.empty() && !fallbackHost.empty()) info.serverIp = fallbackHost;

    if (info.serverIp.empty()) return info;

    std::string host;
    info.signalingUrl =
        BuildSignalingUrl(signalingResourcePath, info.serverIp, &host);
    const std::string effectiveHost = host.empty() ? info.serverIp : host;
    info.signalingServer = effectiveHost.find(':') == std::string::npos
                               ? effectiveHost + ":443"
                               : effectiveHost;
    return info;
}

// ---- client --------------------------------------------------------------------------------

json::Value SignalingClient::PeerInfoMessage(const std::string& peerId) {
    json::Value message = json::ObjectValue();
    json::Value peerInfo = json::ObjectValue();
    peerInfo.Set("id", peerId);
    message.Set("peer_info", std::move(peerInfo));
    return message;
}

json::Value SignalingClient::OfferMessage(const std::string& sdp) {
    json::Value message = json::ObjectValue();
    json::Value peerMessage = json::ObjectValue();
    peerMessage.Set("msg", "offer");
    peerMessage.Set("sdp", sdp);
    message.Set("peer_msg", std::move(peerMessage));
    return message;
}

json::Value SignalingClient::IceMessage(const std::string& candidate, const std::string& mid,
                                        int mLineIndex) {
    json::Value message = json::ObjectValue();
    json::Value peerMessage = json::ObjectValue();
    peerMessage.Set("msg", "candidate");
    peerMessage.Set("candidate", candidate);
    if (!mid.empty()) peerMessage.Set("sdpMid", mid);
    peerMessage.Set("sdpMLineIndex", mLineIndex);
    message.Set("peer_msg", std::move(peerMessage));
    return message;
}

void SignalingClient::SetMessageHandler(ISignalingTransport::MessageHandler handler) {
    if (transport_) transport_->SetMessageHandler(std::move(handler));
}

void SignalingClient::SetClosedHandler(ISignalingTransport::CloseHandler handler) {
    if (transport_) transport_->SetCloseHandler(std::move(handler));
}

void SignalingClient::Send(const json::Value& message) {
    if (!transport_) return;
    transport_->Send(message.Dump());
}

void SignalingClient::Start(const SignalingInfo& info, const std::string& sessionId) {
    sessionId_ = sessionId;
    if (!transport_) return;
    // The seat routes by sub-protocol; without it the socket closes immediately.
    transport_->Connect(info.signalingUrl, "x-nv-sessionid." + sessionId);
}

void SignalingClient::Stop() {
    if (!transport_) return;
    transport_->Close(1000, "client stopped");
}

void SignalingClient::SendPeerInfo(const std::string& peerId) {
    peerId_ = peerId;
    Send(PeerInfoMessage(peerId));
}

void SignalingClient::SendOffer(const std::string& sdp) { Send(OfferMessage(sdp)); }

void SignalingClient::SendAnswer(const std::string& sdp) {
    json::Value message = json::ObjectValue();
    json::Value peerMessage = json::ObjectValue();
    peerMessage.Set("msg", "answer");
    peerMessage.Set("sdp", sdp);
    message.Set("peer_msg", std::move(peerMessage));
    Send(message);
}

void SignalingClient::SendIceCandidate(const std::string& candidate, const std::string& mid,
                                       int mLineIndex) {
    Send(IceMessage(candidate, mid, mLineIndex));
}

}  // namespace opennow::core::gfn
