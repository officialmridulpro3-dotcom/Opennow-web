// OpenNOW native client — DirectX-backed stream panel.

#pragma once

#include "StreamSurfacePanel.g.h"

#include <memory>

#include <opennow/dx/StreamSurface.hpp>

namespace winrt::OpenNOW::implementation {

class StreamSurfacePanel : public StreamSurfacePanelT<StreamSurfacePanel> {
public:
    StreamSurfacePanel();

    // ---- IDL surface ---------------------------------------------------------
    bool Ready() const;
    winrt::hstring LastError();
    double Sharpening();
    void Sharpening(double value);
    bool Letterbox();
    void Letterbox(bool value);

    bool Begin(uint32_t width, uint32_t height);
    bool SubmitNv12(winrt::array_view<uint8_t const> luma, uint32_t lumaStride,
                    winrt::array_view<uint8_t const> chroma, uint32_t chromaStride,
                    uint32_t width, uint32_t height, bool fullRange);
    bool Redraw();
    void Shutdown();

    // ---- internals -------------------------------------------------------------
    // Used by the stream controller to hand the renderer a hardware texture
    // directly (zero-copy) when the decoder can produce one.
    opennow::dx::StreamSurface* Surface() const { return surface_.get(); }

    void OnLoaded(winrt::Windows::Foundation::IInspectable const& sender,
                  Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnUnloaded(winrt::Windows::Foundation::IInspectable const& sender,
                    Microsoft::UI::Xaml::RoutedEventArgs const& args);
    void OnSizeChanged(winrt::Windows::Foundation::IInspectable const& sender,
                       Microsoft::UI::Xaml::SizeChangedEventArgs const& args);

private:
    bool EnsureSurface();

    std::unique_ptr<opennow::dx::StreamSurface> surface_;
    std::string lastError_;
    double sharpening_ = 0.0;
    bool letterbox_ = true;
    uint32_t frameWidth_ = 0;
    uint32_t frameHeight_ = 0;
    bool hasFrame_ = false;
};

}  // namespace winrt::OpenNOW::implementation

namespace winrt::OpenNOW::factory_implementation {

class StreamSurfacePanel
    : public StreamSurfacePanelT<StreamSurfacePanel, implementation::StreamSurfacePanel> {};

}  // namespace winrt::OpenNOW::factory_implementation
