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

// horizontal and vertical Sobel kernels
static const float kernelX[9] = {
    -1.0, 0.0, 1.0,
    -2.0, 0.0, 2.0,
    -1.0, 0.0, 1.0
};
static const float kernelY[9] = {
    -1.0, -2.0, -1.0,
     0.0,  0.0,  0.0,
     1.0,  2.0,  1.0
};

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // Keep the input texture in scope before entering the loops.
    float4 color = inImage[id.xy];

    int2 size = int2(width, height);

    // both kernels over the luminance of the 3x3 neighborhood, neighbors outside the image are clamped to the border
    float gx = 0.0;
    float gy = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 p = clamp(int2(id.xy) + int2(x, y), int2(0, 0), size - 1);
            float lum = dot(inImage[p].rgb, float3(0.2126, 0.7152, 0.0722));
            gx += kernelX[(y + 1) * 3 + (x + 1)] * lum;
            gy += kernelY[(y + 1) * 3 + (x + 1)] * lum;
        }
    }

    // edge strength is the length of the gradient
    float edge = sqrt(gx * gx + gy * gy);

    // strength blends between the original (0) and the white-on-black edge image (1)
    color.rgb = lerp(color.rgb, float3(edge), params.strength);
    outImage[id.xy] = color;
}
