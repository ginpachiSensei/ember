// Maps the HDR scene color to the display: exposure, then a tone curve, then (if needed) sRGB encoding.

#include "core/Common.slang"

struct TonemapPush {
    uint hdrTexture;
    uint sampler;
    uint tonemapper; // 0 none (clip), 1 Reinhard, 2 ACES (fitted), 3 AgX, 4 Khronos PBR Neutral
    uint outputSrgb; // 1: the target is UNORM, so encode sRGB here; 0: the hardware does it
    float exposure;  // linear multiplier (2^EV)
};
[[vk::push_constant]] TonemapPush pc;

// Narkowicz's fit of the ACES filmic curve (2015).
float3 acesFitted(float3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// AgX (Troy Sobotka), polynomial fit by Benjamin Wrensch. Desaturates bright colors gracefully.
float3 agxContrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agx(float3 color)
{
    const float3x3 inset = float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
                                    0.0784335999999992, 0.878468636469772, 0.0784336,
                                    0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outset = float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
                                     -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
                                     -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393, maxEv = 4.026069;
    color = mul(color, inset);
    color = clamp(log2(max(color, 1e-10)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = agxContrast(color);
    color = mul(color, outset);
    // AgX's output is display-encoded; convert back to linear so the sRGB step below applies once.
    return pow(max(color, 0.0), 2.2);
}

// Khronos PBR Neutral: keeps base colors accurate under normal exposure (good for material review).
float3 pbrNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak), g);
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[shader("vertex")]
FullscreenOut vsMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId, 0.0);
}

[shader("fragment")]
float4 psMain(FullscreenOut input) : SV_Target
{
    float3 hdr = sampleTextureLevel(pc.hdrTexture, pc.sampler, input.uv, 0).rgb * pc.exposure;
    float3 color;
    switch (pc.tonemapper) {
    case 1: color = hdr / (1.0 + hdr); break;
    case 2: color = acesFitted(hdr * 0.6); break; // the fit expects pre-exposed input
    case 3: color = agx(hdr); break;
    case 4: color = pbrNeutral(hdr); break;
    default: color = saturate(hdr); break;
    }
    color = saturate(color);
    if (pc.outputSrgb != 0) {
        color = linearToSrgb(color);
    }
    return float4(color, 1.0);
}
