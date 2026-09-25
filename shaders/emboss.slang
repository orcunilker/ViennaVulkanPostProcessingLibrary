/**
 * @file emboss.slang
 * @brief Emboss: 3x3 relief filter along the diagonal.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::EmbossSettings in VVPPL.h.
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

/// Emboss kernel along the diagonal, the weights sum to one so flat areas keep their color.
static const float kernel[9] = {
    -2.0, -1.0, 0.0,
    -1.0,  1.0, 1.0,
     0.0,  1.0, 2.0
};

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // read the center pixel before the neighborhood loop: KosmicKrisp (Vulkan SDK 1.4.350.0) otherwise
    // declares the Metal texture inside the loop and uses it out of scope; its alpha is kept
    float4 color = inImage[id.xy];

    int2 size = int2(width, height);

    // weighted sum over the 3x3 neighborhood, neighbors outside the image are clamped to the border
    float3 sum = float3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 p = clamp(int2(id.xy) + int2(x, y), int2(0, 0), size - 1);
            sum += kernel[(y + 1) * 3 + (x + 1)] * inImage[p].rgb;
        }
    }

    // strength blends between the original (0) and the embossed image (1)
    color.rgb = lerp(color.rgb, max(sum, 0.0), params.strength);
    outImage[id.xy] = color;
}
