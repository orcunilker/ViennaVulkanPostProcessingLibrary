[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float threshold;
    float strength;
}
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    float4 color = inImage[id.xy];

    // mirror every channel above the threshold back down, so the tone curve has no jump
    float3 folded = select(color.rgb > params.threshold, max(2.0 * params.threshold - color.rgb, 0.0), color.rgb);

    // strength blends between the unchanged tones (0) and the full reversal (1)
    color.rgb = lerp(color.rgb, folded, params.strength);
    outImage[id.xy] = color;
}
