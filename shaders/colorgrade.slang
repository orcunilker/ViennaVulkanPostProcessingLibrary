/**
 * @file colorgrade.slang
 * @brief Color grading: gain, lift and gamma per channel, then contrast and saturation.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::ColorGradeSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float saturation;
    float contrast;
    float liftR, liftG, liftB;
    float gammaR, gammaG, gammaB;
    float gainR, gainG, gainB;
};
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    float3 color = inImage[id.xy].rgb;

    // gain and lift per channel, then gamma per channel
    color = color * float3(params.gainR, params.gainG, params.gainB)
              + float3(params.liftR, params.liftG, params.liftB);
    color = pow(max(color, 0.0), 1.0 / float3(params.gammaR, params.gammaG, params.gammaB));

    // contrast around middle grey 0.5, saturation around the luminance
    color = (color - 0.5) * params.contrast + 0.5;
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    color = lerp(float3(luma), color, params.saturation);
    outImage[id.xy] = float4(color, inImage[id.xy].a);
}
