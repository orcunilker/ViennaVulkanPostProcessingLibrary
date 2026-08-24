[[vk::binding(0,0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> inImage;

[[vk::binding(1, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2D<float4> outImage;

struct Params {
    float intensity;
    float radius;
    float smoothness;
};
[[vk::push_constant]] Params params;

[shader("compute")]
[numthreads(8,8,1)]
void main(uint3 id: SV_DispatchThreadID) {
    uint width, height;
    inImage.GetDimensions(width, height);

    if (id.x >= width || id.y >= height) return;

    // Pixelmitte in 0..1. Das +0.5 ist wichtig: id ist der ganzzahlige Pixelindex,
    // die Mitte von Pixel 0 liegt bei 0.5 und nicht bei 0.
    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);

    // Vektor von der Bildmitte weg, Bereich -0.5 bis 0.5
    float2 d = uv - 0.5;

    // Seitenverhaeltnis korrigieren, sonst wird der Kreis zur Ellipse
    d.x *= float(width) / float(height);

    // Abstand zur Mitte in einen Helligkeitsfaktor umrechnen:
    // 1 innerhalb von radius, weich abfallend auf 0 bei radius + smoothness
    float v = 1.0 - smoothstep(params.radius, params.radius + params.smoothness, length(d));

    float4 color = inImage[id.xy];
    // intensity blendet zwischen "kein Effekt" (1.0) und vollem Abfall (v)
    color.rgb *= lerp(1.0, v, params.intensity);
    outImage[id.xy] = color;
}

