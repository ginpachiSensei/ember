#pragma once

// C++ twins of the structs in shaders/core/Common.slang. Same field order, scalar layout
// (no implicit padding on either side), so they can be memcpy'd straight into GPU buffers.

#include "ember/core/Math.h"

namespace ember::render::gpu {

struct Material {
    vec4 baseColorFactor;
    vec3 emissiveFactor;
    f32 metallicFactor;
    f32 roughnessFactor;
    f32 normalScale;
    f32 occlusionStrength;
    f32 alphaCutoff;
    u32 baseColorTexture;
    u32 metallicRoughnessTexture;
    u32 normalTexture;
    u32 occlusionTexture;
    u32 emissiveTexture;
    u32 flags;
    u32 pad0;
    u32 pad1;
};
static_assert(sizeof(Material) == 80);

inline constexpr u32 kMaterialAlphaMask = 1;
inline constexpr u32 kMaterialDoubleSided = 2;

struct FrameData {
    mat4 view;
    mat4 proj;
    mat4 viewProj;
    mat4 invViewProj;
    mat4 lightViewProj;

    vec3 cameraPosition;
    f32 time;

    vec3 sunDirection;
    f32 sunIntensity;
    vec3 sunColor;
    f32 iblIntensity;

    u32 irradianceMap;
    u32 prefilteredMap;
    u32 brdfLut;
    u32 prefilteredMipCount;

    u32 shadowMap;
    u32 linearSampler;
    u32 clampSampler;
    u32 shadowSampler;

    f32 shadowNormalBias;
    f32 shadowTexelSize;
    u32 debugView;
    u32 skyMap;

    vec2 viewportSize;
    u32 proceduralSky;
    f32 skyIntensity;
};
static_assert(sizeof(FrameData) == 5 * 64 + 7 * 16);

struct DrawPush {
    u64 frame;
    u64 vertices;
    u64 materials;
    u32 materialIndex;
    u32 pad;
    mat4 model;
};
static_assert(sizeof(DrawPush) == 96);

struct SkyPush {
    u64 frame;
};

struct TonemapPush {
    u32 hdrTexture;
    u32 sampler;
    u32 tonemapper;
    u32 outputSrgb;
    f32 exposure;
};

struct IblPush {
    u32 source;
    u32 target;
    u32 sampler;
    u32 size;
    f32 roughness;
    f32 sourceSize;
    f32 pad0;
    f32 pad1;
    vec3 sunDirection;
    f32 pad2;
};
static_assert(sizeof(IblPush) == 48);

} // namespace ember::render::gpu
