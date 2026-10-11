// OpenNOW native client — DirectX 10 video surface for XAML.
//
// Why two devices are used, and why that is still "DirectX 10":
//
//   * The *rendering* device is a genuine ID3D10Device1 (DirectX 10.1, falling
//     back to 10.0). All video work — texture upload, colour conversion,
//     sharpening — runs there as shader model 4.0 HLSL. That is the GPU tier we
//     promise to support.
//   * XAML composition (SwapChainPanel) is only wired up for DXGI composition
//     swap chains created against a D3D11 device. Rather than give up on DX10
//     hardware, we create the composition device at feature level 10_1/10_0 —
//     the D3D11 runtime runs fine on every DirectX 10 GPU — and share the
//     rendered frame across as a DXGI shared texture.
//
// Result: one memcpy-free GPU handoff, no D3D11-only features, and a pipeline
// that boots on a 2007-era GeForce 8 as well as on a 4090.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <d3d10_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

namespace opennow::dx {

enum class FrameFormat {
    Nv12,  // semi-planar 4:2:0, what WebRTC/hardware decoders hand us
    I420,  // planar 4:2:0
    Yuv444,
    Bgra,  // already decoded to RGB by the transport
};

struct VideoFrame {
    FrameFormat format = FrameFormat::Nv12;
    int width = 0;
    int height = 0;
    const std::uint8_t* planes[3] = {nullptr, nullptr, nullptr};
    int strides[3] = {0, 0, 0};
    bool fullRange = true;
};

struct AdapterInfo {
    std::wstring description;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    int d3d10FeatureLevel = 0;  // 1000 = 10.0, 1010 = 10.1
    std::uint64_t dedicatedVideoMemory = 0;
    bool directx10Capable = false;
};

// Queries the primary adapter once at startup; drives the "low effects" profile
// and the DirectX diagnostics shown in Settings → About.
AdapterInfo QueryAdapterInfo();

class StreamSurface {
public:
    static std::unique_ptr<StreamSurface> Create();
    ~StreamSurface();

    // Binds the renderer to a WinUI SwapChainPanel. `panel` is the native
    // ISwapChainPanelNative interface obtained from the XAML control.
    bool AttachPanel(void* swapChainPanelNative);
    void DetachPanel();

    bool Resize(int width, int height);
    void SetSharpening(float strength);
    void SetLetterbox(bool letterbox);

    // Uploads a decoded frame and presents it. Safe to call every frame; the
    // uploader reuses staging textures of the same size.
    bool SubmitFrame(const VideoFrame& frame);
    // Presents the last rendered frame again (used when the panel is resized
    // mid-session or when the stream stalls).
    bool Redraw();

    // Zero-copy path: accepts a texture already living on the D3D10 device
    // (hardware decoder output) and skips the CPU upload entirely.
    bool SubmitTexture(ID3D10Texture2D* luma, ID3D10Texture2D* chroma, FrameFormat format,
                       bool fullRange);

    bool initialized() const { return initialized_; }
    std::string lastError() const { return lastError_; }
    int frameWidth() const { return frameWidth_; }
    int frameHeight() const { return frameHeight_; }

private:
    StreamSurface() = default;

    bool CreateDevices();
    bool CreateShaders();
    bool CreateSharedTarget(int width, int height);
    bool CreateSwapChain(int width, int height);
    bool EnsureStaging(const VideoFrame& frame);
    void RenderToSharedTarget();
    void SetError(std::string message);

    bool initialized_ = false;
    std::string lastError_;
    int frameWidth_ = 0;
    int frameHeight_ = 0;
    int panelWidth_ = 0;
    int panelHeight_ = 0;
    float sharpening_ = 0.0f;
    bool letterbox_ = true;
    FrameFormat currentFormat_ = FrameFormat::Nv12;
    bool currentFullRange_ = true;

    Microsoft::WRL::ComPtr<ID3D10Device1> d3d10_;
    Microsoft::WRL::ComPtr<ID3D10Texture2D> lumaTexture_;
    Microsoft::WRL::ComPtr<ID3D10Texture2D> chromaTexture_;
    Microsoft::WRL::ComPtr<ID3D10Texture2D> sharedTarget_;
    Microsoft::WRL::ComPtr<ID3D10RenderTargetView> sharedTargetView_;
    Microsoft::WRL::ComPtr<ID3D10VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D10PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D10Buffer> constantBuffer_;
    Microsoft::WRL::ComPtr<ID3D10SamplerState> pointSampler_;
    Microsoft::WRL::ComPtr<ID3D10SamplerState> linearSampler_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutex_;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d11_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11Context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTextureOn11_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> sharedViewOn11_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> copyVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> copyPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> copyConstantBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> copySampler_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyedMutexOn11_;

    HANDLE sharedHandle_ = nullptr;
};

}  // namespace opennow::dx
