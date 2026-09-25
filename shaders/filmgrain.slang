/**
 * @file filmgrain.slang
 * @brief Film grain: adds random noise that changes with time.
 *
 * Reads binding 0 and writes binding 1; Params matches vvppl::FilmGrainSettings in VVPPL.h.
 */

[[vk::binding(0, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float intensity;
    float time;
};
[[vk::push_constant]] Params params;

/// @brief Integer hash: turns pixel coordinate and time into a pseudo-random number in 0..1.
float hash(uint3 v) {
    uint h = v.x * 374761393u + v.y * 668265263u + v.z * 1274126177u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h = h ^ (h >> 16u);
    return float(h) / 4294967295.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    // time is the third input, so the grain changes every frame
    uint seed = uint(params.time * 1000.0);
    float noise = hash(uint3(id.xy, seed)) - 0.5;

    float4 color = inImage[id.xy];
    color.rgb += noise * params.intensity;
    outImage[id.xy] = color;
}
