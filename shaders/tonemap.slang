/**
 * @file tonemap.slang
 * @brief Tone mapping: maps the HDR color to 0..1 with an ACES curve.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::TonemapSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float exposure;
};
[[vk::push_constant]] Params params;

/// @brief ACES approximation by Narkowicz, one curve instead of the full ACES transform.
float3 acesFilmic(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float4 color = inImage[id.xy];
    // exposure scales the HDR color before the curve compresses it into 0..1
    color.rgb = acesFilmic(color.rgb * params.exposure);
    outImage[id.xy] = color;
}
