/**
 * @file segmentation.slang
 * @brief Hue segmentation: replaces every color by the nearest of a few fully saturated hues.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::SegmentationSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float segments;
    float minChroma;
    float strength;
};
[[vk::push_constant]] Params params;

/// @brief Converts a hue in 0..1 to a fully saturated color.
float3 hueToRgb(float hue) {
    return saturate(abs(frac(hue + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    float4 color = inImage[id.xy];
    float3 c = color.rgb;

    // hue from the largest channel and the difference of the other two
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float chroma = maxC - minC;
    float hue;
    if (maxC == c.r)      hue = (c.g - c.b) / max(chroma, 0.0001);
    else if (maxC == c.g) hue = (c.b - c.r) / max(chroma, 0.0001) + 2.0;
    else                  hue = (c.r - c.g) / max(chroma, 0.0001) + 4.0;
    hue = frac(hue / 6.0);

    // round the hue to the nearest segment center, the first center is pure red
    float count = max(params.segments, 1.0);
    float3 segmentColor = hueToRgb(round(hue * count) / count);

    // colors with too little chroma have no reliable hue and form one segment in middle grey
    if (chroma < params.minChroma) segmentColor = float3(0.18);

    // strength blends between the original (0) and the flat segment colors (1)
    color.rgb = lerp(c, segmentColor, params.strength);
    outImage[id.xy] = color;
}
