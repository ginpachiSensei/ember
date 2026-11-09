#pragma once

// Physically based shading: the metallic-roughness model used by glTF and UE.
// References: Karis, "Real Shading in Unreal Engine 4" (2013); the Filament docs; glTF spec Appendix B.

#include "core/Common.slang"

// GGX / Trowbridge-Reitz normal distribution. alpha = roughness^2.
float D_GGX(float NoH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility term, already divided by 4 NoL NoV.
float V_SmithGGXCorrelated(float NoV, float NoL, float alpha)
{
    float a2 = alpha * alpha;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

float3 F_Schlick(float3 f0, float VoH)
{
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

// Schlick with roughness, for the ambient (IBL) term where there's no single half vector.
float3 F_SchlickRoughness(float3 f0, float NoV, float roughness)
{
    return f0 + (max(float3(1.0 - roughness), f0) - f0) * pow(1.0 - NoV, 5.0);
}

struct Surface {
    float3 baseColor;
    float metallic;
    float roughness; // perceptual, 0..1
    float3 normal;
    float occlusion;
    float3 emissive;
};

// Light reflected towards V from one light of `radiance` arriving from L.
float3 evaluateLight(Surface s, float3 V, float3 L, float3 radiance)
{
    float3 N = s.normal;
    float3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoL = saturate(dot(N, L));
    float NoH = saturate(dot(N, H));
    float VoH = saturate(dot(V, H));

    float alpha = max(s.roughness * s.roughness, 0.002); // avoid a singular highlight at roughness 0
    float3 f0 = lerp(float3(0.04), s.baseColor, s.metallic);

    float3 F = F_Schlick(f0, VoH);
    float3 specular = D_GGX(NoH, alpha) * V_SmithGGXCorrelated(NoV, NoL, alpha) * F;
    float3 diffuse = (1.0 - F) * (1.0 - s.metallic) * s.baseColor / PI;
    return (diffuse + specular) * radiance * NoL;
}

// Hammersley point i of n on the unit square (low-discrepancy samples for importance sampling).
float2 hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), float(reversebits(i)) * 2.3283064365386963e-10);
}

// A GGX-distributed half vector around N for the sample point xi.
float3 importanceSampleGGX(float2 xi, float3 N, float alpha)
{
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}
