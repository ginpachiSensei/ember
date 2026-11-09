// Image-based lighting precomputation (compute). Run when the environment changes, not every frame.
//
//   environment cube  --(cosine convolution)-->  irradiance cube   (diffuse)
//   environment cube  --(GGX prefilter/mip)-->   prefiltered cube  (specular, roughness per mip)
//   (analytic)        ------------------------>  BRDF LUT          (split-sum scale/bias)
//
// Reference: Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH 2013).

#include "core/Common.slang"
#include "core/Pbr.slang"
#include "core/Sky.slang"

struct IblPush {
    uint source;        // input texture (cube, or 2D equirect for csEquirectToCube)
    uint target;        // storage index of the mip being written
    uint sampler;
    uint size;          // target mip size in texels
    float roughness;    // csPrefilter
    float sourceSize;   // source cube mip-0 size, for filtered importance sampling
    float pad0;
    float pad1;
    float3 sunDirection; // csProceduralSky
    float pad2;
};
[[vk::push_constant]] IblPush pc;

// Direction through texel (x, y) of a cube face, matching Vulkan's face orientation table.
float3 cubeDirection(uint face, uint2 texel, uint size)
{
    float2 uv = (float2(texel) + 0.5) / float(size) * 2.0 - 1.0;
    float3 dir;
    switch (face) {
    case 0: dir = float3(1.0, -uv.y, -uv.x); break;  // +X
    case 1: dir = float3(-1.0, -uv.y, uv.x); break;  // -X
    case 2: dir = float3(uv.x, 1.0, uv.y); break;    // +Y
    case 3: dir = float3(uv.x, -1.0, -uv.y); break;  // -Y
    case 4: dir = float3(uv.x, -uv.y, 1.0); break;   // +Z
    default: dir = float3(-uv.x, -uv.y, -1.0); break; // -Z
    }
    return normalize(dir);
}

void writeFace(uint3 id, float3 color)
{
    gStorageArray[NonUniformResourceIndex(pc.target)][id] = float4(color, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csProceduralSky(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.size || id.y >= pc.size) {
        return;
    }
    float3 dir = cubeDirection(id.z, id.xy, pc.size);
    writeFace(id, proceduralSky(dir, normalize(pc.sunDirection)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csEquirectToCube(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.size || id.y >= pc.size) {
        return;
    }
    float3 dir = cubeDirection(id.z, id.xy, pc.size);
    float2 uv = float2(atan2(dir.z, dir.x) / (2.0 * PI) + 0.5, acos(clamp(dir.y, -1.0, 1.0)) / PI);
    float3 color = sampleTextureLevel(pc.source, pc.sampler, uv, 0).rgb;
    writeFace(id, min(color, 64000.0)); // keep HDR sun spikes inside half-float range
}

// Diffuse irradiance: the cosine-weighted average of incoming light over the hemisphere around N.
[shader("compute")]
[numthreads(8, 8, 1)]
void csIrradiance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.size || id.y >= pc.size) {
        return;
    }
    float3 N = cubeDirection(id.z, id.xy, pc.size);
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);

    const uint phiSteps = 64;
    const uint thetaSteps = 24;
    // Read a mip whose texels are about as big as the patch of sky each sample stands for. A sharper
    // mip makes small bright spots (the sun in an HDR photo) alias into ripples on diffuse surfaces.
    float saSample = 2.0 * PI / float(phiSteps * thetaSteps);
    float saTexel = 4.0 * PI / (6.0 * pc.sourceSize * pc.sourceSize);
    float lod = 0.5 * log2(saSample / saTexel) + 1.0;
    float3 sum = 0.0;
    for (uint p = 0; p < phiSteps; ++p) {
        float phi = (float(p) + 0.5) / float(phiSteps) * 2.0 * PI;
        for (uint t = 0; t < thetaSteps; ++t) {
            float theta = (float(t) + 0.5) / float(thetaSteps) * 0.5 * PI;
            float3 local = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 dir = local.x * right + local.y * up + local.z * N;
            float3 radiance = sampleCube(pc.source, pc.sampler, dir, lod).rgb;
            sum += radiance * cos(theta) * sin(theta);
        }
    }
    writeFace(id, PI * sum / float(phiSteps * thetaSteps));
}

// Specular: the environment convolved with the GGX lobe for this mip's roughness (assumes N = V = R).
[shader("compute")]
[numthreads(8, 8, 1)]
void csPrefilter(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.size || id.y >= pc.size) {
        return;
    }
    float3 N = cubeDirection(id.z, id.xy, pc.size);
    if (pc.roughness <= 0.0) {
        writeFace(id, sampleCube(pc.source, pc.sampler, N, 0).rgb); // mirror: plain copy
        return;
    }
    float3 V = N;
    float alpha = pc.roughness * pc.roughness;

    const uint sampleCount = 1024; // fewer leaves ring-shaped banding on rough surfaces
    float3 sum = 0.0;
    float weight = 0.0;
    for (uint i = 0; i < sampleCount; ++i) {
        float3 H = importanceSampleGGX(hammersley(i, sampleCount), N, alpha);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = dot(N, L);
        if (NoL <= 0.0) {
            continue;
        }
        // Filtered importance sampling: read a blurrier mip where samples are sparse, which removes
        // the bright-dot noise you'd otherwise need thousands of samples to average out.
        float NoH = saturate(dot(N, H));
        float pdf = D_GGX(NoH, alpha) * 0.25; // D * NoH / (4 VoH), with N = V
        float saSample = 1.0 / (float(sampleCount) * pdf + 1e-4);
        float saTexel = 4.0 * PI / (6.0 * pc.sourceSize * pc.sourceSize);
        float lod = max(0.5 * log2(saSample / saTexel) + 1.0, 0.0);

        sum += sampleCube(pc.source, pc.sampler, L, lod).rgb * NoL;
        weight += NoL;
    }
    writeFace(id, sum / max(weight, 1e-4));
}

// Split-sum BRDF table: for (NoV, roughness), the scale (x) and bias (y) applied to F0.
[shader("compute")]
[numthreads(8, 8, 1)]
void csBrdfLut(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= pc.size || id.y >= pc.size) {
        return;
    }
    float NoV = max((float(id.x) + 0.5) / float(pc.size), 1e-3);
    float roughness = (float(id.y) + 0.5) / float(pc.size);
    float alpha = roughness * roughness;

    float3 V = float3(sqrt(1.0 - NoV * NoV), 0.0, NoV);
    float3 N = float3(0.0, 0.0, 1.0);
    const uint sampleCount = 1024;
    float a = 0.0;
    float b = 0.0;
    for (uint i = 0; i < sampleCount; ++i) {
        float3 H = importanceSampleGGX(hammersley(i, sampleCount), N, alpha);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        float VoH = saturate(dot(V, H));
        if (NoL > 0.0) {
            // V_SmithGGXCorrelated already includes 1/(4 NoL NoV); undo the pdf (D NoH / 4 VoH).
            float vis = V_SmithGGXCorrelated(NoV, NoL, alpha) * 4.0 * NoL * VoH / max(NoH, 1e-4);
            float fc = pow(1.0 - VoH, 5.0);
            a += (1.0 - fc) * vis;
            b += fc * vis;
        }
    }
    gStorage[NonUniformResourceIndex(pc.target)][id.xy] = float4(a, b, 0.0, 1.0) / float(sampleCount);
}
