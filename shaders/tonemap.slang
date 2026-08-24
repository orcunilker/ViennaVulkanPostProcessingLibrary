[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float exposure;
}
[[vk::push_constant]] Params params;

// ACES-Naeherung nach Narkowicz, eine Kurve statt der vollen ACES-Transformation
float3 acesFilmic(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    float4 color = inImage[id.xy];
    // Exposure skaliert vor der Kurve - danach waere es wirkungslos
    color.rgb = acesFilmic(color.rgb * params.exposure);
    outImage[id.xy] = color;
}

