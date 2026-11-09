// Background: drawn after opaque meshes as a fullscreen triangle at the far plane (depth 0, reverse-Z),
// so it only covers pixels no mesh wrote to.

#include "core/Common.slang"
#include "core/Sky.slang"

struct SkyPush {
    FrameData* frame;
};
[[vk::push_constant]] SkyPush pc;

[shader("vertex")]
FullscreenOut vsMain(uint vertexId : SV_VertexID)
{
    return fullscreenTriangle(vertexId, 0.0);
}

[shader("fragment")]
float4 psMain(FullscreenOut input) : SV_Target
{
    FrameData frame = *pc.frame;
    // Un-project a point on the near plane (depth 1 in reverse-Z) to get the view ray.
    float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearPoint = mul(frame.invViewProj, float4(ndc, 1.0, 1.0));
    float3 dir = normalize(nearPoint.xyz / nearPoint.w - frame.cameraPosition);

    float3 color = sampleCube(frame.skyMap, frame.clampSampler, dir, 0).rgb * frame.skyIntensity;
    if (frame.proceduralSky != 0) {
        color += sunDisk(dir, normalize(frame.sunDirection), frame.sunColor * frame.sunIntensity);
    }
    return float4(color, 1.0);
}
