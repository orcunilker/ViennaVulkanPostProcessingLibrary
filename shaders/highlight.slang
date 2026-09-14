[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float keyR, keyG, keyB;
    float tolerance;
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
    float3 key = float3(params.keyR, params.keyG, params.keyB);

    // cosine between color and key, compares the direction only, so brightness does not matter
    float match = dot(color.rgb, key) / max(length(color.rgb) * length(key), 0.0001);

    // fully selected in the inner half of the tolerance band, fading out in the outer half
    float tolerance = max(params.tolerance, 0.001);
    float selected = smoothstep(1.0 - tolerance, 1.0 - 0.5 * tolerance, match);

    // everything that is not selected loses its saturation
    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 background = lerp(color.rgb, float3(lum), params.strength);
    color.rgb = lerp(background, color.rgb, selected);
    outImage[id.xy] = color;
}
