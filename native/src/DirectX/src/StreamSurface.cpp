// OpenNOW native client — DirectX 10 video surface implementation.

#include "opennow/dx/StreamSurface.hpp"

#include <cstring>

#include <d3dcompiler.h>
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <vector>

#include "opennow/dx/Shaders.hpp"

#pragma comment(lib, "d3d10_1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace opennow::dx {
namespace {

struct ConstantBuffer {
    float flags[4];
    float texelSize[2];
    float reserved[2];
};

// D3D11 copy shaders (used only to blit the shared frame into the XAML swap
// chain). Shader model 4.0 keeps them loadable on a feature-level-10 device.
const char* kCopyVertexShader = R"HLSL(
struct VS_OUTPUT { float4 position : SV_POSITION; float2 texcoord : TEXCOORD0; };
VS_OUTPUT main(uint id : SV_VertexID)
{
    float2 corner = float2((id == 1) ? 3.0 : -1.0, (id == 2) ? 3.0 : -1.0);
    VS_OUTPUT output;
    output.position = float4(corner, 0.0, 1.0);
    output.texcoord = float2((corner.x + 1.0) * 0.5, 1.0 - (corner.y + 1.0) * 0.5);
    return output;
}
)HLSL";

const char* kLetterboxPixelShader = R"HLSL(
cbuffer Frame : register(b0)
{
    // Scale + offset applied to the source rectangle to letterbox it.
    float2 scale;
    float2 offset;
    float4 background;
};
Texture2D frame : register(t0);
SamplerState sampler_ : register(s0);

float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET
{
    float2 source = (texcoord - offset) / scale;
    if (any(source < 0.0) || any(source > 1.0))
    {
        return background;
    }
    return frame.Sample(sampler_, source);
}
)HLSL";

Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(const char* source, const char* entryPoint,
                                               const char* target, std::string& error) {
    Microsoft::WRL::ComPtr<ID3DBlob> compiled;
    Microsoft::WRL::ComPtr<ID3DBlob> messages;
    const HRESULT result =
        D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, entryPoint, target,
                   D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, compiled.GetAddressOf(),
                   messages.GetAddressOf());
    if (FAILED(result)) {
        error = messages != nullptr
                    ? std::string(static_cast<const char*>(messages->GetBufferPointer()),
                                  messages->GetBufferSize())
                    : "D3DCompile failed";
        return nullptr;
    }
    return compiled;
}

}  // namespace

AdapterInfo QueryAdapterInfo() {
    AdapterInfo info;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) return info;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(0, adapter.GetAddressOf()))) return info;

    DXGI_ADAPTER_DESC1 description{};
    if (SUCCEEDED(adapter->GetDesc1(&description))) {
        info.description = description.Description;
        info.vendorId = description.VendorId;
        info.deviceId = description.DeviceId;
        info.dedicatedVideoMemory = description.DedicatedVideoMemory;
    }

    // The real question is whether a D3D10.1 (or at least D3D10.0) device can
    // be created. Anything that passes is a DirectX 10-class GPU.
    Microsoft::WRL::ComPtr<ID3D10Device1> probe;
    if (SUCCEEDED(D3D10CreateDevice1(adapter.Get(), D3D10_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D10_CREATE_DEVICE_BGRA_SUPPORT,
                                     D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION,
                                     probe.GetAddressOf()))) {
        info.d3d10FeatureLevel = 1010;
        info.directx10Capable = true;
        return info;
    }
    if (SUCCEEDED(D3D10CreateDevice1(adapter.Get(), D3D10_DRIVER_TYPE_HARDWARE, nullptr,
                                     D3D10_CREATE_DEVICE_BGRA_SUPPORT,
                                     D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION,
                                     probe.GetAddressOf()))) {
        info.d3d10FeatureLevel = 1000;
        info.directx10Capable = true;
    }
    return info;
}

std::unique_ptr<StreamSurface> StreamSurface::Create() {
    auto surface = std::unique_ptr<StreamSurface>(new StreamSurface());
    if (!surface->CreateDevices()) return nullptr;
    if (!surface->CreateShaders()) return nullptr;
    surface->initialized_ = true;
    return surface;
}

StreamSurface::~StreamSurface() {
    DetachPanel();
    if (sharedHandle_ != nullptr) ::CloseHandle(sharedHandle_);
}

void StreamSurface::SetError(std::string message) { lastError_ = std::move(message); }

bool StreamSurface::CreateDevices() {
    // ---- DirectX 10 rendering device ------------------------------------------
    // Try 10.1 first, then 10.0. BGRA support is required because the shared
    // target we hand to D3D11 is BGRA8.
    UINT createFlags = D3D10_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    createFlags |= D3D10_CREATE_DEVICE_DEBUG;
#endif

    HRESULT result = D3D10CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
                                        D3D10_FEATURE_LEVEL_10_1, D3D10_1_SDK_VERSION,
                                        d3d10_.GetAddressOf());
    if (FAILED(result)) {
        result = D3D10CreateDevice1(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
                                    D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION,
                                    d3d10_.GetAddressOf());
    }
    if (FAILED(result)) {
        SetError("No DirectX 10 device could be created (HRESULT 0x" +
                 std::to_string(static_cast<unsigned>(result)) + ").");
        return false;
    }

    // ---- D3D11 composition device at feature level 10 ---------------------------
    // The D3D11 runtime is present on every OS this app targets (Windows 10
    // 1809+), and a feature-level-10 device runs on DirectX 10 hardware. No
    // D3D11-only feature is used anywhere in this file.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    UINT d3d11Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    d3d11Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, d3d11Flags, levels,
                               static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                               d3d11_.GetAddressOf(), nullptr, d3d11Context_.GetAddressOf());
    if (FAILED(result)) {
        SetError("No DirectX 11 (feature level 10) device could be created (HRESULT 0x" +
                 std::to_string(static_cast<unsigned>(result)) + ").");
        return false;
    }
    return true;
}

bool StreamSurface::CreateShaders() {
    std::string error;
    auto vertexBlob = CompileShader(kVideoVertexShader, "main", "vs_4_0", error);
    if (vertexBlob == nullptr) {
        SetError("Vertex shader failed to compile: " + error);
        return false;
    }
    if (FAILED(d3d10_->CreateVertexShader(vertexBlob->GetBufferPointer(),
                                          vertexBlob->GetBufferSize(),
                                          vertexShader_.GetAddressOf()))) {
        SetError("CreateVertexShader failed.");
        return false;
    }

    auto pixelBlob = CompileShader(kVideoPixelShader, "main", "ps_4_0", error);
    if (pixelBlob == nullptr) {
        SetError("Pixel shader failed to compile: " + error);
        return false;
    }
    if (FAILED(d3d10_->CreatePixelShader(pixelBlob->GetBufferPointer(),
                                         pixelBlob->GetBufferSize(),
                                         pixelShader_.GetAddressOf()))) {
        SetError("CreatePixelShader failed.");
        return false;
    }

    // ---- D3D11 copy pass -----------------------------------------------------------
    auto copyVertexBlob = CompileShader(kCopyVertexShader, "main", "vs_4_0", error);
    auto copyPixelBlob = CompileShader(kLetterboxPixelShader, "main", "ps_4_0", error);
    if (copyVertexBlob == nullptr || copyPixelBlob == nullptr) {
        SetError("Copy shader failed to compile: " + error);
        return false;
    }
    if (FAILED(d3d11_->CreateVertexShader(copyVertexBlob->GetBufferPointer(),
                                          copyVertexBlob->GetBufferSize(), nullptr,
                                          copyVertexShader_.GetAddressOf())) ||
        FAILED(d3d11_->CreatePixelShader(copyPixelBlob->GetBufferPointer(),
                                         copyPixelBlob->GetBufferSize(), nullptr,
                                         copyPixelShader_.GetAddressOf()))) {
        SetError("Failed to create the D3D11 copy shaders.");
        return false;
    }

    // The letterbox shader reads a cbuffer at b0.
    D3D11_BUFFER_DESC bufferDescription{};
    bufferDescription.ByteWidth = sizeof(float) * 16;
    bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(d3d11_->CreateBuffer(&bufferDescription, nullptr, &copyConstantBuffer_))) {
        SetError("Failed to create the D3D11 constant buffer.");
        return false;
    }

    // ---- samplers and the D3D10 constant buffer -------------------------------------
    D3D10_SAMPLER_DESC pointDescription{};
    pointDescription.Filter = D3D10_FILTER_MIN_MAG_MIP_POINT;
    pointDescription.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
    pointDescription.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
    pointDescription.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(d3d10_->CreateSamplerState(&pointDescription, pointSampler_.GetAddressOf()))) {
        SetError("CreateSamplerState (point) failed.");
        return false;
    }

    D3D10_SAMPLER_DESC linearDescription = pointDescription;
    linearDescription.Filter = D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (FAILED(d3d10_->CreateSamplerState(&linearDescription, linearSampler_.GetAddressOf()))) {
        SetError("CreateSamplerState (linear) failed.");
        return false;
    }

    D3D10_BUFFER_DESC constantDescription{};
    constantDescription.ByteWidth = sizeof(ConstantBuffer);
    constantDescription.Usage = D3D10_USAGE_DYNAMIC;
    constantDescription.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    constantDescription.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;
    if (FAILED(d3d10_->CreateBuffer(&constantDescription, nullptr,
                                    constantBuffer_.GetAddressOf()))) {
        SetError("Failed to create the D3D10 constant buffer.");
        return false;
    }

    D3D11_SAMPLER_DESC copySamplerDescription{};
    copySamplerDescription.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    copySamplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    copySamplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    copySamplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    if (FAILED(d3d11_->CreateSamplerState(&copySamplerDescription, copySampler_.GetAddressOf()))) {
        SetError("Failed to create the D3D11 sampler.");
        return false;
    }
    return true;
}

bool StreamSurface::CreateSharedTarget(int width, int height) {
    sharedTarget_.Reset();
    sharedTargetView_.Reset();
    keyedMutex_.Reset();
    sharedTextureOn11_.Reset();
    sharedViewOn11_.Reset();
    keyedMutexOn11_.Reset();
    if (sharedHandle_ != nullptr) {
        ::CloseHandle(sharedHandle_);
        sharedHandle_ = nullptr;
    }

    D3D10_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D10_USAGE_DEFAULT;
    // SHARED_KEYEDMUTEX lets the D3D10 renderer and the D3D11 presenter take
    // turns without either stalling the GPU pipeline.
    description.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
    description.MiscFlags = D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    if (FAILED(d3d10_->CreateTexture2D(&description, nullptr, sharedTarget_.GetAddressOf()))) {
        SetError("Failed to create the shared render target.");
        return false;
    }
    if (FAILED(d3d10_->CreateRenderTargetView(sharedTarget_.Get(), nullptr,
                                              sharedTargetView_.GetAddressOf()))) {
        SetError("Failed to create the shared render target view.");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIResource> resource;
    if (FAILED(sharedTarget_->QueryInterface(IID_PPV_ARGS(resource.GetAddressOf())))) {
        SetError("Shared render target is not an IDXGIResource.");
        return false;
    }
    if (FAILED(resource->GetSharedHandle(&sharedHandle_))) {
        SetError("GetSharedHandle failed.");
        return false;
    }
    if (FAILED(sharedTarget_->QueryInterface(IID_PPV_ARGS(keyedMutex_.GetAddressOf())))) {
        SetError("Shared render target has no keyed mutex.");
        return false;
    }

    // Open the same texture on the composition device.
    if (FAILED(d3d11_->OpenSharedResource(sharedHandle_, IID_PPV_ARGS(
                                                             sharedTextureOn11_.GetAddressOf())))) {
        SetError("OpenSharedResource failed on the composition device.");
        return false;
    }
    if (FAILED(d3d11_->CreateShaderResourceView(sharedTextureOn11_.Get(), nullptr,
                                                sharedViewOn11_.GetAddressOf()))) {
        SetError("Failed to create the shared shader resource view.");
        return false;
    }
    sharedTextureOn11_->QueryInterface(IID_PPV_ARGS(keyedMutexOn11_.GetAddressOf()));
    return true;
}

bool StreamSurface::CreateSwapChain(int width, int height) {
    swapChain_.Reset();

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(d3d11_.As(&dxgiDevice))) {
        SetError("Composition device is not DXGI capable.");
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        SetError("GetAdapter failed.");
        return false;
    }
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        SetError("GetParent(IDXGIFactory2) failed.");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // Two buffers: enough for flip-sequential without extra latency.
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    if (FAILED(factory->CreateSwapChainForComposition(d3d11_.Get(), &description, nullptr,
                                                      swapChain_.GetAddressOf()))) {
        SetError("CreateSwapChainForComposition failed.");
        return false;
    }
    return true;
}

bool StreamSurface::AttachPanel(void* swapChainPanelNative) {
    if (swapChainPanelNative == nullptr) {
        SetError("AttachPanel called with a null panel.");
        return false;
    }
    if (panelWidth_ == 0 || panelHeight_ == 0) {
        panelWidth_ = 1920;
        panelHeight_ = 1080;
    }
    if (!CreateSwapChain(panelWidth_, panelHeight_)) return false;

    auto* panel = static_cast<ISwapChainPanelNative*>(swapChainPanelNative);
    if (FAILED(panel->SetSwapChain(swapChain_.Get()))) {
        SetError("ISwapChainPanelNative::SetSwapChain failed.");
        return false;
    }
    return true;
}

void StreamSurface::DetachPanel() {
    if (swapChain_ != nullptr) {
        swapChain_->SetFullscreenState(FALSE, nullptr);
        swapChain_.Reset();
    }
}

bool StreamSurface::Resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (width == panelWidth_ && height == panelHeight_ && swapChain_ != nullptr) return true;

    panelWidth_ = width;
    panelHeight_ = height;
    if (swapChain_ == nullptr) return true;

    // ResizeBuffers on a composition swap chain requires all references to the
    // back buffers to be released first.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    d3d11Context_->OMSetRenderTargets(0, nullptr, nullptr);
    backBuffer.Reset();

    const HRESULT result = swapChain_->ResizeBuffers(2, static_cast<UINT>(width),
                                                     static_cast<UINT>(height),
                                                     DXGI_FORMAT_B8G8R8A8_UNORM, 0);
    if (FAILED(result)) {
        SetError("ResizeBuffers failed (HRESULT 0x" +
                 std::to_string(static_cast<unsigned>(result)) + ").");
        return CreateSwapChain(width, height);
    }
    return true;
}

void StreamSurface::SetSharpening(float strength) {
    sharpening_ = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
}

void StreamSurface::SetLetterbox(bool letterbox) { letterbox_ = letterbox; }

bool StreamSurface::EnsureStaging(const VideoFrame& frame) {
    const bool sizeChanged = frameWidth_ != frame.width || frameHeight_ != frame.height;
    if (!sizeChanged && lumaTexture_ != nullptr && currentFormat_ == frame.format) return true;

    frameWidth_ = frame.width;
    frameHeight_ = frame.height;
    currentFormat_ = frame.format;
    lumaTexture_.Reset();
    chromaTexture_.Reset();

    // Staging textures are CPU-writable; the shader reads them directly. On a
    // DX10 device this is one upload per frame, which is what the web client's
    // WebRTC path also does when it cannot get a hardware handle.
    const DXGI_FORMAT lumaFormat =
        frame.format == FrameFormat::Bgra ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8_UNORM;

    D3D10_TEXTURE2D_DESC lumaDescription{};
    lumaDescription.Width = static_cast<UINT>(frame.width);
    lumaDescription.Height = static_cast<UINT>(frame.height);
    lumaDescription.MipLevels = 1;
    lumaDescription.ArraySize = 1;
    lumaDescription.Format = lumaFormat;
    lumaDescription.SampleDesc.Count = 1;
    lumaDescription.Usage = D3D10_USAGE_DYNAMIC;
    lumaDescription.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    lumaDescription.CPUAccessFlags = D3D10_CPU_ACCESS_WRITE;

    if (FAILED(d3d10_->CreateTexture2D(&lumaDescription, nullptr, lumaTexture_.GetAddressOf()))) {
        SetError("Failed to create the luma staging texture.");
        return false;
    }

    if (frame.format == FrameFormat::Nv12 || frame.format == FrameFormat::I420 ||
        frame.format == FrameFormat::Yuv444) {
        const int chromaWidth = frame.format == FrameFormat::Yuv444 ? frame.width : frame.width / 2;
        const int chromaHeight =
            frame.format == FrameFormat::Yuv444 ? frame.height : frame.height / 2;
        const int chromaChannels = frame.format == FrameFormat::Nv12 ? 2 : 1;

        D3D10_TEXTURE2D_DESC chromaDescription = lumaDescription;
        chromaDescription.Width = static_cast<UINT>(chromaWidth);
        chromaDescription.Height = static_cast<UINT>(chromaHeight);
        // NV12 interleaves U/V per pixel, so it needs a two-channel format.
        chromaDescription.Format = chromaChannels == 2 ? DXGI_FORMAT_R8G8_UNORM
                                                       : DXGI_FORMAT_R8_UNORM;
        if (FAILED(d3d10_->CreateTexture2D(&chromaDescription, nullptr,
                                           chromaTexture_.GetAddressOf()))) {
            SetError("Failed to create the chroma staging texture.");
            return false;
        }
    }

    // The shared target matches the stream resolution; letterboxing happens in
    // the D3D11 copy pass so resizing the window never re-allocates it.
    return CreateSharedTarget(frame.width, frame.height);
}

bool StreamSurface::SubmitFrame(const VideoFrame& frame) {
    if (!initialized_ || frame.width <= 0 || frame.height <= 0) return false;
    if (!EnsureStaging(frame)) return false;

    currentFullRange_ = frame.fullRange;

    D3D10_MAPPED_TEXTURE2D mapped{};
    if (FAILED(lumaTexture_->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &mapped))) {
        SetError("Failed to map the luma staging texture.");
        return false;
    }
    const std::uint8_t* source = frame.planes[0];
    const int rowBytes = frame.strides[0] != 0 ? frame.strides[0] : frame.width;
    for (int row = 0; row < frame.height; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(row) * mapped.RowPitch,
                    source + static_cast<std::size_t>(row) * rowBytes,
                    static_cast<std::size_t>(rowBytes));
    }
    lumaTexture_->Unmap(0);

    if (chromaTexture_ != nullptr && frame.planes[1] != nullptr) {
        D3D10_MAPPED_TEXTURE2D chromaMapped{};
        if (FAILED(chromaTexture_->Map(0, D3D10_MAP_WRITE_DISCARD, 0, &chromaMapped))) {
            SetError("Failed to map the chroma staging texture.");
            return false;
        }
        const int chromaHeight = frame.format == FrameFormat::Yuv444 ? frame.height
                                                                     : frame.height / 2;
        const int chromaStride = frame.strides[1] != 0 ? frame.strides[1] : frame.width / 2;
        for (int row = 0; row < chromaHeight; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(chromaMapped.pData) +
                            static_cast<std::size_t>(row) * chromaMapped.RowPitch,
                        frame.planes[1] + static_cast<std::size_t>(row) * chromaStride,
                        static_cast<std::size_t>(chromaStride));
        }
        chromaTexture_->Unmap(0);
    }

    RenderToSharedTarget();
    return Redraw();
}

bool StreamSurface::SubmitTexture(ID3D10Texture2D* luma, ID3D10Texture2D* chroma,
                                  FrameFormat format, bool fullRange) {
    // Zero-copy path: the decoder already produced D3D10 textures, so we only
    // swap the shader resources and skip the CPU upload entirely.
    if (!initialized_ || luma == nullptr) return false;

    D3D10_TEXTURE2D_DESC description{};
    luma->GetDesc(&description);
    frameWidth_ = static_cast<int>(description.Width);
    frameHeight_ = static_cast<int>(description.Height);
    currentFormat_ = format;
    currentFullRange_ = fullRange;

    lumaTexture_ = luma;
    chromaTexture_ = chroma;
    if (sharedTarget_ == nullptr || frameWidth_ != static_cast<int>(description.Width)) {
        if (!CreateSharedTarget(frameWidth_, frameHeight_)) return false;
    }
    RenderToSharedTarget();
    return Redraw();
}

void StreamSurface::RenderToSharedTarget() {
    if (sharedTargetView_ == nullptr) return;

    // Take ownership of the shared texture for the duration of the draw.
    if (keyedMutex_ != nullptr) keyedMutex_->AcquireSync(0, INFINITE);

    D3D10_VIEWPORT viewport{};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<UINT>(frameWidth_);
    viewport.Height = static_cast<UINT>(frameHeight_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    d3d10_->RSSetViewports(1, &viewport);
    d3d10_->OMSetRenderTargets(1, sharedTargetView_.GetAddressOf(), nullptr);

    ConstantBuffer constants{};
    constants.flags[0] = static_cast<float>(currentFormat_);
    constants.flags[1] = currentFullRange_ ? 1.0f : 0.0f;
    constants.flags[2] = sharpening_;
    constants.flags[3] = 0.0f;
    constants.texelSize[0] = 1.0f / static_cast<float>(frameWidth_);
    constants.texelSize[1] = 1.0f / static_cast<float>(frameHeight_);

    void* mapped = nullptr;
    if (SUCCEEDED(constantBuffer_->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped, &constants, sizeof(constants));
        constantBuffer_->Unmap();
    }

    // Samplers and shader resources: luma on t0, chroma on t1.
    ID3D10ShaderResourceView* views[2] = {nullptr, nullptr};
    Microsoft::WRL::ComPtr<ID3D10ShaderResourceView> lumaView;
    Microsoft::WRL::ComPtr<ID3D10ShaderResourceView> chromaView;

    D3D10_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = currentFormat_ == FrameFormat::Bgra ? DXGI_FORMAT_B8G8R8A8_UNORM
                                                                 : DXGI_FORMAT_R8_UNORM;
    viewDescription.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MipLevels = 1;
    d3d10_->CreateShaderResourceView(lumaTexture_.Get(), &viewDescription,
                                     lumaView.GetAddressOf());

    if (chromaTexture_ != nullptr) {
        D3D10_SHADER_RESOURCE_VIEW_DESC chromaViewDescription = viewDescription;
        chromaViewDescription.Format = currentFormat_ == FrameFormat::Nv12
                                           ? DXGI_FORMAT_R8G8_UNORM
                                           : DXGI_FORMAT_R8_UNORM;
        d3d10_->CreateShaderResourceView(chromaTexture_.Get(), &chromaViewDescription,
                                         chromaView.GetAddressOf());
    }

    views[0] = lumaView.Get();
    views[1] = chromaView.Get();
    d3d10_->PSSetShaderResources(0, 2, views);

    ID3D10SamplerState* samplers[2] = {pointSampler_.Get(), linearSampler_.Get()};
    d3d10_->PSSetSamplers(0, 2, samplers);

    ID3D10Buffer* constantBuffers[1] = {constantBuffer_.Get()};
    d3d10_->PSSetConstantBuffers(0, 1, constantBuffers);

    d3d10_->VSSetShader(vertexShader_.Get());
    d3d10_->PSSetShader(pixelShader_.Get());
    d3d10_->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d10_->Draw(3, 0);

    d3d10_->Flush();
    if (keyedMutex_ != nullptr) keyedMutex_->ReleaseSync(0);
}

bool StreamSurface::Redraw() {
    if (swapChain_ == nullptr || sharedViewOn11_ == nullptr) {
        SetError("Redraw called before the panel was attached.");
        return false;
    }

    if (keyedMutexOn11_ != nullptr) keyedMutexOn11_->AcquireSync(0, INFINITE);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf())))) {
        if (keyedMutexOn11_ != nullptr) keyedMutexOn11_->ReleaseSync(0);
        SetError("GetBuffer failed.");
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targetView;
    if (FAILED(d3d11_->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                              targetView.GetAddressOf()))) {
        if (keyedMutexOn11_ != nullptr) keyedMutexOn11_->ReleaseSync(0);
        SetError("CreateRenderTargetView failed.");
        return false;
    }

    // Letterbox math: keep the stream's aspect ratio inside the panel.
    const float panelAspect = panelWidth_ > 0 && panelHeight_ > 0
                                  ? static_cast<float>(panelWidth_) /
                                        static_cast<float>(panelHeight_)
                                  : 16.0f / 9.0f;
    const float frameAspect = frameHeight_ > 0 ? static_cast<float>(frameWidth_) /
                                                     static_cast<float>(frameHeight_)
                                               : panelAspect;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    if (letterbox_ && frameAspect > 0.0f) {
        if (frameAspect > panelAspect) {
            scaleY = panelAspect / frameAspect;
            offsetY = (1.0f - scaleY) * 0.5f;
        } else {
            scaleX = frameAspect / panelAspect;
            offsetX = (1.0f - scaleX) * 0.5f;
        }
    }

    struct CopyConstants {
        float scale[2];
        float offset[2];
        float background[4];
    } copyConstants{};
    copyConstants.scale[0] = scaleX;
    copyConstants.scale[1] = scaleY;
    copyConstants.offset[0] = offsetX;
    copyConstants.offset[1] = offsetY;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(d3d11Context_->Map(copyConstantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                     &mapped))) {
        std::memcpy(mapped.pData, &copyConstants, sizeof(copyConstants));
        d3d11Context_->Unmap(copyConstantBuffer_.Get(), 0);
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(panelWidth_);
    viewport.Height = static_cast<float>(panelHeight_);
    viewport.MaxDepth = 1.0f;
    d3d11Context_->RSSetViewports(1, &viewport);

    const float clearColour[4] = {0.027f, 0.043f, 0.071f, 1.0f};  // --color-void
    d3d11Context_->ClearRenderTargetView(targetView.Get(), clearColour);
    d3d11Context_->OMSetRenderTargets(1, targetView.GetAddressOf(), nullptr);

    d3d11Context_->VSSetShader(copyVertexShader_.Get(), nullptr, 0);
    d3d11Context_->PSSetShader(copyPixelShader_.Get(), nullptr, 0);
    d3d11Context_->PSSetShaderResources(0, 1, sharedViewOn11_.GetAddressOf());
    d3d11Context_->PSSetSamplers(0, 1, copySampler_.GetAddressOf());
    d3d11Context_->PSSetConstantBuffers(0, 1, copyConstantBuffer_.GetAddressOf());
    d3d11Context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    d3d11Context_->Draw(3, 0);

    d3d11Context_->Flush();
    swapChain_->Present(1, 0);

    // Release the back-buffer view before the next Present so flip-sequential
    // can recycle the buffer.
    ID3D11RenderTargetView* nullView = nullptr;
    d3d11Context_->OMSetRenderTargets(1, &nullView, nullptr);
    if (keyedMutexOn11_ != nullptr) keyedMutexOn11_->ReleaseSync(0);
    return true;
}

}  // namespace opennow::dx
