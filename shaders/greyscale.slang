[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

[shader("compute")]
[numthreads(8,8,1)]

void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    float4 color = inImage[id.xy];
    float lum = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    outImage[id.xy] = float4(lum, lum, lum, color.a);
}

