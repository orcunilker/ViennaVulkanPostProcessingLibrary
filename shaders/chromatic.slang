[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float intensity;
}
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    int2 size = int2(width, height);
    float2 uv = (float2(id.xy) + 0.5) / float2(size);

    // Versatz waechst nach aussen und ist in der Bildmitte null
    float2 offset = (uv - 0.5) * params.intensity;

    // Rot nach aussen, Blau nach innen, Gruen bleibt an Ort und Stelle
    int2 pR = clamp(int2((uv + offset) * float2(size)), int2(0, 0), size - 1);
    int2 pB = clamp(int2((uv - offset) * float2(size)), int2(0, 0), size - 1);

    float4 color = inImage[id.xy];
    color.r = inImage[pR].r;
    color.b = inImage[pB].b;
    outImage[id.xy] = color;
}

