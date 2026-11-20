// Post-processing, second pass: finishes both blurs vertically, adds the bloom, then grades
// (exposure, contrast, saturation), darkens the corners (vignette), and adds film grain
// and CRT scanlines. With neutral settings every pixel comes out as it went in.

Texture2D<float4> blurred : register(t0, space2);
Texture2D<float4> bright : register(t1, space2);
SamplerState blurred_sampler : register(s0, space2);
SamplerState bright_sampler : register(s1, space2);

cbuffer Look : register(b0, space3)
{
    float4 size;  // x: width, y: height
    float4 tilt;  // x: focus centre, y: half the sharp band, z: strongest blur (pixels), w: ramp
    float4 bloom; // x: threshold, y: intensity, z: radius (pixels)
    float4 grade; // x: exposure, y: contrast, z: saturation, w: vignette
    float4 tint;  // the light's colour: r, g, b multiplied
    float4 film;  // x: grain, y: scanlines, z: the frame (for the grain)
};

// A number from 0 to 1 for a pixel and a frame, the same each time.
float noise(int x, int y, float frame)
{
    return frac(sin(dot(float3(x, y, frame), float3(12.9898, 78.233, 37.719))) * 43758.5453);
}

float tilt_radius(float y)
{
    const float from_focus = abs((y + 0.5) / size.y - tilt.x) - tilt.y;
    return from_focus <= 0.0 ? 0.0 : saturate(from_focus / max(tilt.w, 0.0001)) * tilt.z;
}

int row(int y)
{
    return clamp(y, 0, (int)size.y - 1);
}

float4 main(float4 position : SV_Position) : SV_Target0
{
    const int x = (int)position.x;
    const int y = (int)position.y;

    float3 c;
    const float radius = tilt_radius((float)y);
    if (radius < 0.5)
    {
        c = blurred.Load(int3(x, y, 0)).rgb;
    }
    else
    {
        const int reach = min((int)ceil(radius), 12);
        const float sigma = radius / 2.0;
        float3 sum = 0.0;
        float total = 0.0;
        [loop]
        for (int i = -reach; i <= reach; ++i)
        {
            const float w = exp(-(float)(i * i) / (2.0 * sigma * sigma));
            sum += blurred.Load(int3(x, row(y + i), 0)).rgb * w;
            total += w;
        }
        c = sum / total;
    }

    if (bloom.y > 0.0)
    {
        const int reach = clamp((int)ceil(bloom.z), 0, 16);
        const float sigma = max(bloom.z / 2.0, 0.5);
        float3 sum = 0.0;
        float total = 0.0;
        [loop]
        for (int i = -reach; i <= reach; ++i)
        {
            const float w = exp(-(float)(i * i) / (2.0 * sigma * sigma));
            sum += bright.Load(int3(x, row(y + i), 0)).rgb * w;
            total += w;
        }
        c += bloom.y * (sum / total);
    }

    c *= grade.x;
    c = (c - 0.5) * grade.y + 0.5;
    const float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(float3(luma, luma, luma), c, grade.z);
    c *= tint.rgb;
    if (grade.w > 0.0)
    {
        // Squared distance from the centre, 1 at the corners.
        const float2 from_centre = ((position.xy) / size.xy - 0.5) * 2.0;
        c *= 1.0 - grade.w * saturate(dot(from_centre, from_centre) / 2.0);
    }
    if (film.x > 0.0)
    {
        c += film.x * 0.15 * (noise(x, y, film.z) - 0.5);
    }
    if (film.y > 0.0 && (y % 2) == 1)
    {
        c *= 1.0 - (0.35 * film.y);
    }
    return float4(saturate(c), 1.0);
}
