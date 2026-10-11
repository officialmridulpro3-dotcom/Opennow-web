// OpenNOW native client — signaling transport.
//
// Once CloudMatch allocates a seat, the client opens a WebSocket to
// wss://{seat}/nvst/ with the sub-protocol "x-nv-sessionid.{sessionId}" and
// exchanges the peer_info / SDP / ICE messages defined in
// `src/server/gfn/signaling.ts`. The message framing is ported exactly; only
// the socket implementation is platform specific (WinRT MessageWebSocket).

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "opennow/core/Json.hpp"

namespace opennow::core::gfn {

struct SignalingInfo {
    std::string serverIp;
    std::string signalingServer;   // host:443
    std::string signalingUrl;      // wss://host/nvst/
    std::vector<std::string> rtspsEndpoints;
};

// Port of `resolveSignaling` / `buildSignalingUrl`.
std::string ExtractHostFromUrl(const std::string& url);
std::string BuildSignalingUrl(const std::string& resourcePath, const std::string& serverIp,
                              std::string* hostOut = nullptr);
SignalingInfo ResolveSignaling(const json::Value& payload);

// ---- transport ---------------------------------------------------------------------

class ISignalingTransport {
public:
    using MessageHandler = std::function<void(const std::string& text)>;
    using CloseHandler = std::function<void(int code, const std::string& reason)>;

    virtual ~ISignalingTransport() = default;
    virtual void Connect(const std::string& url, const std::string& subProtocol) = 0;
    virtual void Send(const std::string& text) = 0;
    virtual void Close(int code, const std::string& reason) = 0;

    void SetMessageHandler(MessageHandler handler) { message_ = std::move(handler); }
    void SetCloseHandler(CloseHandler handler) { close_ = std::move(handler); }

protected:
    void EmitMessage(const std::string& text) const {
        if (message_) message_(text);
    }
    void EmitClose(int code, const std::string& reason) const {
        if (close_) close_(code, reason);
    }

private:
    MessageHandler message_;
    CloseHandler close_;
};

using SignalingTransportPtr = std::shared_ptr<ISignalingTransport>;

SignalingTransportPtr CreateSignalingTransport();

// ---- client ------------------------------------------------------------------------

class SignalingClient {
public:
    explicit SignalingClient(SignalingTransportPtr transport)
        : transport_(std::move(transport)) {}

    void Start(const SignalingInfo& info, const std::string& sessionId);
    void Stop();

    // Sends our peer id first — NVIDIA expects a peer_info handshake before it
    // forwards anything the seat sends.
    void SendPeerInfo(const std::string& peerId);
    void SendOffer(const std::string& sdp);
    void SendAnswer(const std::string& sdp);
    void SendIceCandidate(const std::string& candidate, const std::string& mid, int mLineIndex);

    void SetMessageHandler(ISignalingTransport::MessageHandler handler);
    void SetClosedHandler(ISignalingTransport::CloseHandler handler);

    // Test seam: Records what would have gone on the wire.
    static json::Value PeerInfoMessage(const std::string& peerId);
    static json::Value OfferMessage(const std::string& sdp);
    static json::Value IceMessage(const std::string& candidate, const std::string& mid,
                                  int mLineIndex);

private:
    void Send(const json::Value& message);

    SignalingTransportPtr transport_;
    std::string sessionId_;
    std::string peerId_;
};

}  // namespace opennow::core::gfn
