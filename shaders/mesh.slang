// Forward PBR mesh shading: GGX direct sun light with PCF shadows, plus image-based lighting.

#include "core/Common.slang"
#include "core/Pbr.slang"

[[vk::push_constant]] DrawPush pc;

struct VSOut {
    float4 position : SV_Position;
    float3 worldPos : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

// SV_VulkanVertexID includes the draw's vertexOffset. SV_VertexID would subtract it (D3D rules),
// and every mesh after the first in a shared vertex buffer would read the wrong vertices.
[shader("vertex")]
VSOut vsMain(uint vertexId : SV_VulkanVertexID)
{
    Vertex v = pc.vertices[vertexId];
    float4 world = mul(pc.model, float4(v.position, 1.0));

    VSOut o;
    o.position = mul(pc.frame->viewProj, world);
    o.worldPos = world.xyz;
    o.normal = mul(cofactor(pc.model), v.normal);
    o.uv = v.uv;
    return o;
}

// Normal mapping without stored tangents: build the tangent frame from screen-space derivatives.
// Schüler, "Normal Mapping Without Precomputed Tangents" (2013).
float3 perturbNormal(float3 N, float3 worldPos, float2 uv, float3 mapNormal)
{
    float3 dp1 = ddx(worldPos);
    float3 dp2 = ddy(worldPos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);

    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invMax = rsqrt(max(dot(T, T), dot(B, B)));
    if (!(invMax < 1e10)) {
        return N; // degenerate uvs
    }
    // glTF normal maps are +Y up in uv space where v points down, hence -B.
    float3x3 tbn = float3x3(T * invMax, -B * invMax, N);
    return normalize(mul(mapNormal, tbn));
}

float sampleShadow(FrameData frame, float3 worldPos, float3 N, float3 L)
{
    // Offset along the normal (more at grazing angles) to fight shadow acne.
    float NoL = saturate(dot(N, L));
    float3 offset = N * frame.shadowNormalBias * (1.0 - NoL * 0.5);
    float4 clip = mul(frame.lightViewProj, float4(worldPos + offset, 1.0));
    float3 ndc = clip.xyz / clip.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) {
        return 1.0; // outside the shadow map: lit
    }

    // 5x5 PCF with hardware 2x2 filtering per tap = soft edges.
    Texture2D<float> map = gTexturesFloat[NonUniformResourceIndex(frame.shadowMap)];
    SamplerComparisonState cmp = gCompareSamplers[NonUniformResourceIndex(frame.shadowSampler)];
    float sum = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            float2 o = float2(x, y) * frame.shadowTexelSize;
            sum += map.SampleCmpLevelZero(cmp, uv + o, ndc.z);
        }
    }
    return sum / 25.0;
}

float3 ambientLighting(FrameData frame, Surface s, float3 V)
{
    float3 N = s.normal;
    float NoV = max(dot(N, V), 1e-4);
    float3 R = reflect(-V, N);
    float3 f0 = lerp(float3(0.04), s.baseColor, s.metallic);

    // Split-sum approximation: prefiltered radiance * (F0 * A + B) from the BRDF lookup table.
    float3 F = F_SchlickRoughness(f0, NoV, s.roughness);
    float2 brdf = sampleTextureLevel(frame.brdfLut, frame.clampSampler, float2(NoV, s.roughness), 0).rg;
    float lod = s.roughness * float(frame.prefilteredMipCount - 1);
    float3 prefiltered = sampleCube(frame.prefilteredMap, frame.clampSampler, R, lod).rgb;
    float3 specular = prefiltered * (f0 * brdf.x + brdf.y);

    float3 irradiance = sampleCube(frame.irradianceMap, frame.clampSampler, N, 0).rgb;
    float3 diffuse = irradiance * s.baseColor * (1.0 - F) * (1.0 - s.metallic);

    return (diffuse + specular) * s.occlusion * frame.iblIntensity;
}

[shader("fragment")]
float4 psMain(VSOut input, bool frontFace : SV_IsFrontFace) : SV_Target
{
    FrameData frame = *pc.frame;
    Material m = pc.materials[pc.materialIndex];

    float4 baseColor = sampleTexture(m.baseColorTexture, frame.linearSampler, input.uv) * m.baseColorFactor;
    if ((m.flags & MATERIAL_ALPHA_MASK) != 0 && baseColor.a < m.alphaCutoff) {
        discard;
    }

    float4 mr = sampleTexture(m.metallicRoughnessTexture, frame.linearSampler, input.uv);
    float3 mapNormal = sampleTexture(m.normalTexture, frame.linearSampler, input.uv).xyz * 2.0 - 1.0;
    mapNormal.xy *= m.normalScale;
    float occlusion = sampleTexture(m.occlusionTexture, frame.linearSampler, input.uv).r;
    float3 emissive = sampleTexture(m.emissiveTexture, frame.linearSampler, input.uv).rgb * m.emissiveFactor;

    float3 N = normalize(input.normal);
    if (!frontFace) {
        N = -N; // double-sided materials: light the back face as its own surface
    }
    N = perturbNormal(N, input.worldPos, input.uv, normalize(mapNormal));

    Surface s;
    s.baseColor = baseColor.rgb;
    s.metallic = saturate(mr.b * m.metallicFactor);
    s.roughness = clamp(mr.g * m.roughnessFactor, 0.03, 1.0);
    s.normal = N;
    s.occlusion = lerp(1.0, occlusion, m.occlusionStrength);
    s.emissive = emissive;

    float3 V = normalize(frame.cameraPosition - input.worldPos);
    float3 L = normalize(frame.sunDirection);
    float shadow = sampleShadow(frame, input.worldPos, normalize(input.normal), L);

    float3 color = evaluateLight(s, V, L, frame.sunColor * frame.sunIntensity * shadow);
    color += ambientLighting(frame, s, V);
    color += s.emissive;

    switch (frame.debugView) {
    case 1: color = s.baseColor; break;
    case 2: color = N * 0.5 + 0.5; break;
    case 3: color = float3(s.roughness); break;
    case 4: color = float3(s.metallic); break;
    case 5: color = float3(s.occlusion); break;
    case 6: color = float3(shadow); break;
    default: break;
    }
    return float4(color, 1.0);
}

// ---------------------------------------------------------------------------------------------
// Shadow pass: depth only, from the sun.
// ---------------------------------------------------------------------------------------------

struct ShadowOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
ShadowOut vsShadow(uint vertexId : SV_VulkanVertexID)
{
    Vertex v = pc.vertices[vertexId];
    ShadowOut o;
    o.position = mul(pc.frame->lightViewProj, mul(pc.model, float4(v.position, 1.0)));
    o.uv = v.uv;
    return o;
}

// Only used for alpha-masked materials (leaves, fences), so their holes let light through.
[shader("fragment")]
void psShadowMasked(ShadowOut input)
{
    Material m = pc.materials[pc.materialIndex];
    float alpha = sampleTexture(m.baseColorTexture, pc.frame->linearSampler, input.uv).a * m.baseColorFactor.a;
    if (alpha < m.alphaCutoff) {
        discard;
    }
}
