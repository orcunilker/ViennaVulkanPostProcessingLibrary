/**
 * @file vignette.slang
 * @brief Vignette: darkens the image towards the corners.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::VignetteSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float intensity;
    float radius;
    float smoothness;
};
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // pixel center in 0..1; the + 0.5 matters because id is the integer pixel index
    // and the center of pixel 0 lies at 0.5, not at 0
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);

    // vector from the image center, -0.5..0.5
    float2 d = uv - 0.5;

    // correct the aspect ratio, otherwise the circle becomes an ellipse
    d.x *= float(width) / float(height);

    // brightness factor from the distance to the center:
    // 1 within radius, falling smoothly to 0 at radius + smoothness
    float v = 1.0 - smoothstep(params.radius, params.radius + params.smoothness, length(d));

    float4 color = inImage[id.xy];
    // intensity blends between no effect (1.0) and the full falloff (v)
    color.rgb *= lerp(1.0, v, params.intensity);
    outImage[id.xy] = color;
}
