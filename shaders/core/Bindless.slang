#pragma once

// The global bindless table (descriptor set 0). Must match engine/rhi/src/vulkan/VulkanCommon.h.
// Several typed views share each binding: a texture index is valid for whichever view
// matches how the texture was created (2D or cube).

[[vk::binding(0, 0)]] Texture2D<float4> gTextures[];
[[vk::binding(0, 0)]] Texture2D<float> gTexturesFloat[]; // depth textures, for SampleCmp
[[vk::binding(0, 0)]] TextureCube<float4> gCubes[];

[[vk::binding(1, 0)]] SamplerState gSamplers[];
[[vk::binding(1, 0)]] SamplerComparisonState gCompareSamplers[];

// Storage images: one index per mip. Cubes are written as 2D arrays with one layer per face.
[[vk::binding(2, 0)]] RWTexture2D<float4> gStorage[];
[[vk::binding(2, 0)]] RWTexture2DArray<float4> gStorageArray[];

float4 sampleTexture(uint texture, uint sampler, float2 uv)
{
    return gTextures[NonUniformResourceIndex(texture)].Sample(gSamplers[NonUniformResourceIndex(sampler)], uv);
}

float4 sampleTextureLevel(uint texture, uint sampler, float2 uv, float lod)
{
    return gTextures[NonUniformResourceIndex(texture)].SampleLevel(gSamplers[NonUniformResourceIndex(sampler)], uv, lod);
}

float4 sampleCube(uint texture, uint sampler, float3 dir, float lod)
{
    return gCubes[NonUniformResourceIndex(texture)].SampleLevel(gSamplers[NonUniformResourceIndex(sampler)], dir, lod);
}
