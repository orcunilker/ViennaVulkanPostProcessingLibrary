[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float threshold;
}
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // invert every channel above the threshold, values above 1.0 are clamped to black
    float4 color = inImage[id.xy];
    color.rgb = select(color.rgb > params.threshold, max(1.0 - color.rgb, 0.0), color.rgb);
    outImage[id.xy] = color;
}
