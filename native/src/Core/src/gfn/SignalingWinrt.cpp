// OpenNOW native client — platform signaling transport.
//
// Windows: WinRT MessageWebSocket, which gives us the system proxy, TLS and
// keep-alive handling for free and needs no third-party WebSocket library.
// Other platforms: a no-op transport so the portable Core tests still link.

#include "opennow/core/gfn/Signaling.hpp"

#ifdef _WIN32

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include "opennow/core/Logging.hpp"

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;

namespace opennow::core::gfn {
namespace {

class WinRtSignalingTransport final : public ISignalingTransport {
public:
    void Connect(const std::string& url, const std::string& subProtocol) override {
        try {
            socket_ = MessageWebSocket();
            socket_.Control().MessageType(SocketMessageType::Utf8);
            socket_.Control().SupportedProtocols().Append(winrt::to_hstring(subProtocol));

            messageToken_ = socket_.MessageReceived(
                {this, &WinRtSignalingTransport::OnMessageReceived});
            closedToken_ = socket_.Closed({this, &WinRtSignalingTransport::OnClosed});

            const Uri uri(winrt::to_hstring(url));
            // Detached: the socket reports progress through its events, and
            // SignalingClient owns this object for the life of the session.
            ConnectAsync(uri);
        } catch (const winrt::hresult_error& error) {
            LogError("Signaling", "WebSocket connect threw: " +
                                      winrt::to_string(error.message()));
            EmitClose(1006, winrt::to_string(error.message()));
        }
    }

    void Send(const std::string& text) override {
        if (socket_ == nullptr || writer_ == nullptr) return;
        try {
            writer_.WriteString(winrt::to_hstring(text));
            SendAsync();
        } catch (const winrt::hresult_error& error) {
            LogError("Signaling", "WebSocket send threw: " + winrt::to_string(error.message()));
        }
    }

    void Close(int code, const std::string& reason) override {
        if (socket_ == nullptr) return;
        try {
            socket_.Close(static_cast<std::uint16_t>(code), winrt::to_hstring(reason));
        } catch (const winrt::hresult_error&) {
            // Closing a socket that already failed is not interesting.
        }
        socket_ = nullptr;
        writer_ = nullptr;
    }

private:
    fire_and_forget ConnectAsync(Uri uri) {
        try {
            co_await socket_.ConnectAsync(uri);
            writer_ = DataWriter(socket_.OutputStream());
            LogInfo("Signaling", "Signaling socket connected.");
        } catch (const winrt::hresult_error& error) {
            LogError("Signaling", "Signaling connect failed: " +
                                      winrt::to_string(error.message()));
            EmitClose(1006, winrt::to_string(error.message()));
        }
    }

    fire_and_forget SendAsync() {
        try {
            co_await writer_.StoreAsync();
            co_await writer_.FlushAsync();
        } catch (const winrt::hresult_error& error) {
            LogError("Signaling", "Signaling send failed: " + winrt::to_string(error.message()));
        }
    }

    void OnMessageReceived(const MessageWebSocket&, const MessageWebSocketMessageReceivedEventArgs& args) {
        try {
            DataReader reader = args.GetDataReader();
            reader.UnicodeEncoding(UnicodeEncoding::Utf8);
            const std::uint32_t length = reader.UnconsumedBufferLength();
            const std::string text = winrt::to_string(reader.ReadString(length));
            EmitMessage(text);
        } catch (const winrt::hresult_error& error) {
            LogError("Signaling", "Failed to read message: " + winrt::to_string(error.message()));
        }
    }

    void OnClosed(const IWebSocket&, const WebSocketClosedEventArgs& args) {
        EmitClose(args.Code(), winrt::to_string(args.Reason()));
    }

    MessageWebSocket socket_{nullptr};
    DataWriter writer_{nullptr};
    winrt::event_token messageToken_{};
    winrt::event_token closedToken_{};
};

}  // namespace

SignalingTransportPtr CreateSignalingTransport() {
    return std::make_shared<WinRtSignalingTransport>();
}

}  // namespace opennow::core::gfn

#else

#include "opennow/core/Logging.hpp"

namespace opennow::core::gfn {
namespace {

// Only used by the portable unit tests; the product target is Windows.
class NullSignalingTransport final : public ISignalingTransport {
public:
    void Connect(const std::string& url, const std::string&) override {
        LogWarn("Signaling", "WebSocket transport not implemented on this platform: " + url);
    }
    void Send(const std::string&) override {}
    void Close(int, const std::string&) override {}
};

}  // namespace

SignalingTransportPtr CreateSignalingTransport() {
    return std::make_shared<NullSignalingTransport>();
}

}  // namespace opennow::core::gfn

#endif
