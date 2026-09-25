/**
 * @file speedlines.slang
 * @brief Speed lines: white radial lines around the image center.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::SpeedLinesSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float intensity;
    float lineCount;
    float radius;
    float time;
};
[[vk::push_constant]] Params params;

static const float PI = 3.14159265;

/// @brief Integer hash as in the film grain effect: turns line index, time step and a salt into 0..1.
float hash(uint3 v) {
    uint h = v.x * 374761393u + v.y * 668265263u + v.z * 1274126177u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return float(h) / 4294967295.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // vector from the image center, corrected for the aspect ratio
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float2 d = uv - 0.5;
    d.x *= float(width) / float(height);

    // the angle around the center in 0..1 decides which line a pixel belongs to
    float angle = atan2(d.y, d.x) / (2.0 * PI) + 0.5;
    uint index = uint(angle * params.lineCount);

    // every line picks a new width and start distance 15 times per second
    uint tick = uint(params.time * 15.0);
    float lineWidth = 0.5 * hash(uint3(index, tick, 0u));
    float start = params.radius + 0.3 * hash(uint3(index, tick, 1u));

    // tent profile across the line: 1 in its middle, 0 at the edge of its width
    float across = abs(frac(angle * params.lineCount) - 0.5) * 2.0;
    float profile = saturate(1.0 - across / max(lineWidth, 0.001));

    // lines fade in beyond their start distance, so the center stays clear
    float fade = smoothstep(start, start + 0.2, length(d));

    // blend towards white
    float4 color = inImage[id.xy];
    color.rgb = lerp(color.rgb, float3(1.0), profile * fade * params.intensity);
    outImage[id.xy] = color;
}
