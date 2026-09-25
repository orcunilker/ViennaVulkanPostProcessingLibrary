/**
 * @file greyscale.slang
 * @brief Greyscale: replaces the color by its luminance.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::GreyscaleSettings in VVPPL.h.
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

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    float4 color = inImage[id.xy];
    // Rec. 709 luminance, strength blends from the original (0) to greyscale (1)
    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = lerp(color.rgb, float3(lum, lum, lum), params.strength);
    outImage[id.xy] = color;
}
