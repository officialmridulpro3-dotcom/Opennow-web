// OpenNOW native client — DirectX 10 shader sources.
//
// Compiled at runtime with D3DCompile (d3dcompiler_47 ships with Windows) so
// the build needs no FXC step. Targets are vs_4_0 / ps_4_0: shader model 4.0 is
// the highest profile a genuine D3D10 device accepts, and using it is what
// keeps this renderer working on DirectX 10-class GPUs instead of silently
// requiring DirectX 11 hardware.

#pragma once

namespace opennow::dx {

// Full-screen triangle: no vertex buffer, positions come from the SV_VertexID.
// Three vertices covering the viewport is cheaper than a quad and avoids the
// diagonal-seam shading artefacts of two triangles.
inline constexpr const char* kVideoVertexShader = R"HLSL(
struct VS_OUTPUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VS_OUTPUT main(uint vertexId : SV_VertexID)
{
    // (-1,-1) (3,-1) (-1,3) covers the clip-space square with one triangle.
    float2 corner = float2((vertexId == 1) ? 3.0 : -1.0, (vertexId == 2) ? 3.0 : -1.0);
    VS_OUTPUT output;
    output.position = float4(corner, 0.0, 1.0);
    // Clip space Y is flipped relative to texture space.
    output.texcoord = float2((corner.x + 1.0) * 0.5, 1.0 - (corner.y + 1.0) * 0.5);
    return output;
}
)HLSL";

// Pixel shader converts the decoder output to RGB and applies the optional
// sharpening pass in the same draw, which costs one texture fetch instead of a
// second full-screen pass — the difference is measurable on DX10-class GPUs.
inline constexpr const char* kVideoPixelShader = R"HLSL(
cbuffer Constants : register(b0)
{
    // x: format (0 = NV12, 1 = I420, 2 = BGRA, 3 = YUV444 planar)
    // y: full range (1) or video range (0)
    // z: sharpening strength (0..1)
    // w: unused
    float4 flags;
    // Texel size of the luma plane, for the sharpening kernel.
    float2 texelSize;
    float2 reserved;
};

Texture2D lumaPlane : register(t0);
Texture2D chromaPlane : register(t1);
SamplerState pointSampler : register(s0);
SamplerState linearSampler : register(s1);

static const float3x3 kYuvToRgbFull =
{
    1.0,  0.0,      1.402,
    1.0, -0.344136, -0.714136,
    1.0,  1.772,     0.0
};

static const float3x3 kYuvToRgbVideo =
{
    1.164383,  0.0,       1.596027,
    1.164383, -0.391762, -0.812968,
    1.164383,  2.017232,  0.0
};

float3 SampleYuv(float2 uv)
{
    float y = lumaPlane.Sample(pointSampler, uv).x;
    float2 chroma = chromaPlane.Sample(linearSampler, uv).xy;

    if (flags.x > 2.5)
    {
        // YUV444: chroma carries full-resolution Cb/Cr.
        float u = lumaPlane.Sample(pointSampler, uv).y;
        float v = lumaPlane.Sample(pointSampler, uv).z;
        chroma = float2(u, v);
    }

    float3 yuv = float3(y, chroma.x, chroma.y);
    float3 offset = (flags.y > 0.5) ? float3(0.0, -0.5, -0.5) : float3(-0.062745, -0.5, -0.5);
    float3x3 matrix_ = (flags.y > 0.5) ? kYuvToRgbFull : kYuvToRgbVideo;
    return mul(matrix_, yuv + offset);
}

float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD0) : SV_TARGET
{
    float3 colour;

    if (flags.x > 1.5)
    {
        // Already RGB (BGRA/RGBA): straight copy.
        colour = lumaPlane.Sample(linearSampler, texcoord).rgb;
    }
    else
    {
        colour = SampleYuv(texcoord);
    }

    // Unsharp mask with a 4-tap neighbourhood. Cheap, and it restores the edge
    // detail H.264/HEVC loses at the bitrates cloud gaming actually uses.
    float sharpen = flags.z;
    if (sharpen > 0.001)
    {
        float3 blur = 0.25 * (
            lumaPlane.Sample(linearSampler, texcoord + float2(texelSize.x, 0.0)).rgb +
            lumaPlane.Sample(linearSampler, texcoord - float2(texelSize.x, 0.0)).rgb +
            lumaPlane.Sample(linearSampler, texcoord + float2(0.0, texelSize.y)).rgb +
            lumaPlane.Sample(linearSampler, texcoord - float2(0.0, texelSize.y)).rgb);
        colour += (colour - blur) * sharpen * 2.0;
    }

    return float4(saturate(colour), 1.0);
}
)HLSL";

}  // namespace opennow::dx
