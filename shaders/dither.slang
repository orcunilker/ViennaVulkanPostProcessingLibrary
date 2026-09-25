/**
 * @file dither.slang
 * @brief Dithering: adds a 4x4 ordered pattern in sRGB to hide banding in 8-bit output.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::DitherSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float strength;
};
[[vk::push_constant]] Params params;

/// 4x4 Bayer matrix: the thresholds 0..15 spread evenly over every 4x4 block.
static const float bayer[16] = {
    0.0, 8.0, 2.0, 10.0,
    12.0, 4.0, 14.0, 6.0,
    3.0, 11.0, 1.0, 9.0,
    15.0, 7.0, 13.0, 5.0
};

/// @brief Linear to sRGB; the pattern is added in sRGB, where 8-bit output is quantized.
float3 linearToSrgb(float3 c) {
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

/// @brief sRGB to linear.
float3 srgbToLinear(float3 c) {
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float4 color = inImage[id.xy];
    // threshold in -0.5..0.5 of one 8-bit step
    float threshold = (bayer[(id.y % 4) * 4 + id.x % 4] + 0.5) / 16.0 - 0.5;
    float3 srgb = linearToSrgb(color.rgb) + threshold * params.strength / 255.0;
    color.rgb = srgbToLinear(srgb);
    outImage[id.xy] = color;
}
