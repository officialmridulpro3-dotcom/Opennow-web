// OpenNOW native client — DirectX-backed stream panel.

#include "pch.h"

#include "StreamSurfacePanel.h"

#include <microsoft.ui.xaml.media.dxinterop.h>

#include <opennow/core/Logging.hpp>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::OpenNOW::Controls::implementation {

bool StreamSurfacePanel::EnsureSurface() {
    if (surface_ != nullptr) return true;

    surface_ = opennow::dx::StreamSurface::Create();
    if (surface_ == nullptr) {
        lastError_ = "DirectX 10 device creation failed.";
        opennow::core::LogError("StreamSurface", lastError_);
        return false;
    }

    // ISwapChainPanelNative is the interop interface that lets a DXGI swap
    // chain be presented inside the XAML tree.
    winrt::com_ptr<ISwapChainPanelNative> panelNative;
    if (FAILED(as<IUnknown>()->QueryInterface(IID_PPV_ARGS(panelNative.put()))) ||
        panelNative == nullptr) {
        lastError_ = "SwapChainPanel does not expose ISwapChainPanelNative.";
        opennow::core::LogError("StreamSurface", lastError_);
        return false;
    }

    if (!surface_->AttachPanel(panelNative.get())) {
        lastError_ = surface_->lastError();
        opennow::core::LogError("StreamSurface", lastError_);
        return false;
    }

    surface_->SetSharpening(static_cast<float>(sharpening_));
    surface_->SetLetterbox(letterbox_);
    return true;
}

bool StreamSurfacePanel::Ready() const { return surface_ != nullptr && surface_->initialized(); }

winrt::hstring StreamSurfacePanel::LastError() { return winrt::to_hstring(lastError_); }

double StreamSurfacePanel::Sharpening() { return sharpening_; }

void StreamSurfacePanel::Sharpening(double value) {
    sharpening_ = value;
    if (surface_ != nullptr) surface_->SetSharpening(static_cast<float>(value));
}

bool StreamSurfacePanel::Letterbox() { return letterbox_; }

void StreamSurfacePanel::Letterbox(bool value) {
    letterbox_ = value;
    if (surface_ != nullptr) surface_->SetLetterbox(value);
}

bool StreamSurfacePanel::Begin(uint32_t width, uint32_t height) {
    if (!EnsureSurface()) return false;
    frameWidth_ = width;
    frameHeight_ = height;
    hasFrame_ = false;
    return true;
}

bool StreamSurfacePanel::SubmitNv12(winrt::array_view<uint8_t const> luma, uint32_t lumaStride,
                                    winrt::array_view<uint8_t const> chroma,
                                    uint32_t chromaStride, uint32_t width, uint32_t height,
                                    bool fullRange) {
    if (!EnsureSurface()) return false;
    if (luma.empty() || width == 0 || height == 0) return false;

    opennow::dx::VideoFrame frame;
    frame.format = opennow::dx::FrameFormat::Nv12;
    frame.width = static_cast<int>(width);
    frame.height = static_cast<int>(height);
    frame.planes[0] = luma.data();
    frame.strides[0] = lumaStride != 0 ? static_cast<int>(lumaStride) : static_cast<int>(width);
    frame.planes[1] = chroma.empty() ? nullptr : chroma.data();
    frame.strides[1] = chromaStride != 0 ? static_cast<int>(chromaStride) : static_cast<int>(width);
    frame.fullRange = fullRange;

    const bool ok = surface_->SubmitFrame(frame);
    if (!ok) {
        lastError_ = surface_->lastError();
        return false;
    }
    hasFrame_ = true;
    return true;
}

bool StreamSurfacePanel::Redraw() {
    if (surface_ == nullptr || !hasFrame_) return false;
    return surface_->Redraw();
}

void StreamSurfacePanel::Shutdown() {
    if (surface_ != nullptr) {
        surface_->DetachPanel();
        surface_.reset();
    }
    hasFrame_ = false;
}

StreamSurfacePanel::StreamSurfacePanel() {
    // The renderer is created lazily on the first frame so constructing the
    // page never blocks on device creation.
    Loaded({this, &StreamSurfacePanel::OnLoaded});
    Unloaded({this, &StreamSurfacePanel::OnUnloaded});
    SizeChanged({this, &StreamSurfacePanel::OnSizeChanged});
}

void StreamSurfacePanel::OnLoaded(winrt::Windows::Foundation::IInspectable const&,
                                  Microsoft::UI::Xaml::RoutedEventArgs const&) {
    EnsureSurface();
}

void StreamSurfacePanel::OnUnloaded(winrt::Windows::Foundation::IInspectable const&,
                                    Microsoft::UI::Xaml::RoutedEventArgs const&) {
    Shutdown();
}

void StreamSurfacePanel::OnSizeChanged(winrt::Windows::Foundation::IInspectable const&,
                                       Microsoft::UI::Xaml::SizeChangedEventArgs const& args) {
    if (surface_ == nullptr) return;
    surface_->Resize(static_cast<int>(args.NewSize().Width),
                     static_cast<int>(args.NewSize().Height));
    if (hasFrame_) surface_->Redraw();
}

}  // namespace winrt::OpenNOW::Controls::implementation
