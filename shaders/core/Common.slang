#pragma once

// Types shared with C++. Every struct here has a twin in engine/render/src/GpuTypes.h with the
// same field order; shaders are compiled with scalar layout, so no hidden padding is added.

#include "core/Bindless.slang"

static const float PI = 3.14159265359;

struct Vertex {
    float3 position;
    float3 normal;
    float2 uv;
};

static const uint MATERIAL_ALPHA_MASK = 1;
static const uint MATERIAL_DOUBLE_SIDED = 2;

struct Material {
    float4 baseColorFactor;
    float3 emissiveFactor;
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint baseColorTexture;
    uint metallicRoughnessTexture; // glTF: G = roughness, B = metallic
    uint normalTexture;
    uint occlusionTexture;
    uint emissiveTexture;
    uint flags;
    uint pad0;
    uint pad1;
};

struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 invViewProj;
    float4x4 lightViewProj; // world -> shadow map clip space

    float3 cameraPosition;
    float time;

    float3 sunDirection; // points towards the sun
    float sunIntensity;
    float3 sunColor;
    float iblIntensity;

    uint irradianceMap;
    uint prefilteredMap;
    uint brdfLut;
    uint prefilteredMipCount;

    uint shadowMap;
    uint linearSampler;    // repeat, anisotropic
    uint clampSampler;     // clamp to edge, linear
    uint shadowSampler;    // comparison sampler

    float shadowNormalBias;
    float shadowTexelSize;
    uint debugView;
    uint skyMap;

    float2 viewportSize;
    uint proceduralSky;
    float skyIntensity;
};

// Push constants for mesh draws (main and shadow passes).
struct DrawPush {
    FrameData* frame;
    Vertex* vertices;
    Material* materials;
    uint materialIndex;
    uint pad;
    float4x4 model;
};

// A triangle that covers the whole screen: vertex ids 0, 1, 2 -> uv (0,0), (2,0), (0,2).
struct FullscreenOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0; // (0,0) top-left
};

FullscreenOut fullscreenTriangle(uint vertexId, float depth)
{
    FullscreenOut o;
    o.uv = float2((vertexId << 1) & 2, vertexId & 2);
    o.position = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, depth, 1.0);
    return o;
}

// Transpose of the inverse of the upper 3x3, up to scale: transforms normals correctly under non-uniform scale.
// m[row][col] in Slang, whatever the memory layout.
float3x3 cofactor(float4x4 m)
{
    float3 a0 = float3(m[0][0], m[1][0], m[2][0]); // column 0
    float3 a1 = float3(m[0][1], m[1][1], m[2][1]);
    float3 a2 = float3(m[0][2], m[1][2], m[2][2]);
    // The cofactor matrix's columns are cross products of pairs of columns.
    float3 k0 = cross(a1, a2);
    float3 k1 = cross(a2, a0);
    float3 k2 = cross(a0, a1);
    return float3x3(k0.x, k1.x, k2.x, // the constructor takes rows
                    k0.y, k1.y, k2.y,
                    k0.z, k1.z, k2.z);
}
