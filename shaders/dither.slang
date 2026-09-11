[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float strength;
}
[[vk::push_constant]] Params params;

static const float bayer[16] = {
    0.0, 8.0, 2.0, 10.0,
    12.0, 4.0, 14.0, 6.0,
    3.0, 11.0, 1.0, 9.0,
    15.0, 7.0, 13.0, 5.0
};

float3 linearToSrgb(float3 c) {
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

float3 srgbToLinear(float3 c) {
    return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID) {
    float4 color = inImage[id.xy];
    float threshold = (bayer[(id.y % 4) * 4 + id.x % 4] + 0.5) / 16.0 - 0.5;
    float3 srgb = linearToSrgb(color.rgb) + threshold * params.strength / 255.0;
    color.rgb = srgbToLinear(srgb);
    outImage[id.xy] = color;
}