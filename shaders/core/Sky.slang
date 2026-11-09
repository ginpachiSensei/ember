#pragma once

// A cheap analytic daylight sky: a horizon-to-zenith gradient tinted by the sun's height,
// a glow around the sun and a dark ground. Not physically exact, but it gives image-based
// lighting a believable, sun-consistent environment without an HDR file.

float3 proceduralSky(float3 dir, float3 sunDir)
{
    float sunHeight = sunDir.y;
    float y = dir.y;

    // Day colors fade towards a warm sunset tint as the sun gets low.
    float day = saturate(sunHeight * 3.0 + 0.2);
    float3 zenith = lerp(float3(0.02, 0.03, 0.08), float3(0.18, 0.35, 0.80), day);
    float3 horizon = lerp(float3(0.30, 0.15, 0.08), float3(0.65, 0.75, 0.90), day);

    float t = pow(saturate(y), 0.45);
    float3 sky = lerp(horizon, zenith, t);

    // Mie-like glow around the sun.
    float cosSun = saturate(dot(dir, sunDir));
    float3 glowColor = lerp(float3(1.0, 0.45, 0.15), float3(1.0, 0.9, 0.7), day);
    sky += glowColor * (pow(cosSun, 8.0) * 0.6 + pow(cosSun, 64.0) * 1.5) * saturate(sunHeight + 0.3);

    // Ground: dark, slightly lit by the sky, with a soft transition at the horizon.
    float3 ground = float3(0.12, 0.11, 0.10) * (0.3 + 0.7 * day);
    float groundBlend = saturate(-y * 8.0);
    return lerp(sky, ground, groundBlend) * 2.0;
}

// The sun's disk, added on top of the sky in the background pass only (the direct sun light
// is evaluated analytically, so it must not also be baked into the IBL).
float3 sunDisk(float3 dir, float3 sunDir, float3 sunRadiance)
{
    float cosAngle = dot(dir, sunDir);
    float edge = smoothstep(0.99990, 0.99996, cosAngle); // ~0.5 degree radius
    return sunRadiance * edge * 40.0;
}
