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
    color.rgb = 1.0 - color.rgb; // invertieren, Alpha unangetastet lassen
    outImage[id.xy] = color;
}

