// World pixel shader: the texel times the vertex colour plus the point lights, then linear
// distance fog. Opaque quads cut alpha out, so pixel art keeps hard edges and sorts by
// depth; see-through quads (the blended pipeline sets fog_range.w) keep the texel's alpha
// times the colour's, to blend over what is behind them.
// Textures and samplers are in (t, space2) and (s, space2), uniforms in (b, space3), as SDL
// GPU requires.

Texture2D<float4> world_texture : register(t0, space2);
Texture2D<float> shadow_map : register(t1, space2); // depth seen from the sun
SamplerState world_sampler : register(s0, space2);
SamplerState shadow_sampler : register(s1, space2);

#define MAX_POINT_LIGHTS 16 // render::max_point_lights

cbuffer Frame : register(b0, space3)
{
    float4 fog_color;  // rgb
    float4 fog_range;  // x: start distance, y: 1 / (end - start), z: 1 if fog is on,
                       // w: 1 for see-through quads
    float4 light_count; // x
    float4 light_position[MAX_POINT_LIGHTS]; // xyz: where, w: radius
    float4 light_color[MAX_POINT_LIGHTS];    // rgb
    float4 shadow_row0; // the sun's view-projection, by rows
    float4 shadow_row1;
    float4 shadow_row2;
    float4 shadow_row3;
    float4 shadow_params; // x: 1 if on, y: darkness, z: one texel, w: depth bias
    float4 sun;           // xyz: towards the sun
};

// How much of the sun reaches `world`: 1 in the open, down to 1 - darkness where
// something stands between it and the sun. Faces turned away from the sun are already
// dark from their shading, so they are left alone; billboards (wrap) always take it.
float sunlit(float3 world, float4 surface)
{
    if (shadow_params.x < 0.5)
    {
        return 1.0;
    }
    const float normal_length = length(surface.xyz);
    const bool has_normal = normal_length > 0.0001 && surface.w < 0.5;
    const float3 normal = has_normal ? surface.xyz / normal_length : sun.xyz;
    if (has_normal && dot(normal, sun.xyz) <= 0.0)
    {
        return 1.0;
    }
    // A little off the surface, so it does not shadow itself.
    const float4 p = float4(world + normal * 0.02, 1.0);
    const float4 clip = float4(dot(shadow_row0, p), dot(shadow_row1, p), dot(shadow_row2, p),
                               dot(shadow_row3, p));
    const float3 ndc = clip.xyz / clip.w;
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z > 1.0)
    {
        return 1.0;
    }
    // Nine samples around it, for a soft edge.
    float lit = 0.0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float depth = shadow_map.SampleLevel(
                shadow_sampler, uv + float2(dx, dy) * shadow_params.z, 0.0);
            lit += ndc.z - shadow_params.w <= depth ? 1.0 : 0.0;
        }
    }
    return 1.0 - shadow_params.y * (1.0 - lit / 9.0);
}

struct Input
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float distance : TEXCOORD2;
    float3 world : TEXCOORD3;
    float4 surface : TEXCOORD4;
    float3 albedo : TEXCOORD5;
    float4 flash : TEXCOORD6;
    float4 outline : TEXCOORD7;
    float4 frame : TEXCOORD8;
    float4 params : TEXCOORD9;
};

// The texel at `uv`, clear outside the source (an outlined quad reaches a texel beyond it).
float4 texel_at(float2 uv, float4 frame)
{
    if (uv.x < frame.x || uv.y < frame.y || uv.x > frame.z || uv.y > frame.w)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return world_texture.SampleLevel(world_sampler, uv, 0.0);
}

// A number from 0 to 1 for a texel, the same every frame (as the sprite shader's).
float scatter(float2 cell)
{
    return frac(sin(dot(cell, float2(12.9898, 78.233))) * 43758.5453);
}

float4 main(Input input) : SV_Target0
{
    const float2 texel_size = input.params.yz;
    if (input.params.x > 0.0 && scatter(floor(input.uv / texel_size)) < input.params.x)
    {
        discard;
    }
    // Only an outlined quad reaches past its source: elsewhere texels are sampled as
    // they are.
    const bool bounded = input.outline.a > 0.0;
    float4 texel = bounded ? texel_at(input.uv, input.frame)
                           : world_texture.Sample(world_sampler, input.uv);
    if (input.outline.a > 0.0 && texel.a < 0.5)
    {
        const float4 f = input.frame;
        const float near = max(max(texel_at(input.uv + float2(texel_size.x, 0.0), f).a,
                                   texel_at(input.uv - float2(texel_size.x, 0.0), f).a),
                               max(texel_at(input.uv + float2(0.0, texel_size.y), f).a,
                                   texel_at(input.uv - float2(0.0, texel_size.y), f).a));
        if (near >= 0.5)
        {
            texel = float4(input.outline.rgb, input.outline.a);
        }
    }
    const bool blended = fog_range.w > 0.5;
    const float alpha = blended ? texel.a * input.color.a : 1.0;
    if ((blended && alpha < 0.002) || (!blended && texel.a < 0.5))
    {
        discard;
    }
    float3 light = input.color.rgb * sunlit(input.world, input.surface);
    const float normal_length = length(input.surface.xyz);
    if (normal_length > 0.0001)
    {
        const float3 normal = input.surface.xyz / normal_length;
        const float wrap = input.surface.w;
        const int count = (int)light_count.x;
        [loop]
        for (int i = 0; i < count; ++i)
        {
            const float3 to_light = light_position[i].xyz - input.world;
            const float distance = length(to_light);
            const float radius = light_position[i].w;
            if (distance >= radius)
            {
                continue;
            }
            // render::light_falloff(): (1 - (d / r)^2)^2.
            const float x = distance / radius;
            const float falloff = (1.0 - x * x) * (1.0 - x * x);
            const float facing = distance > 0.0001 ? dot(normal, to_light / distance) : 1.0;
            const float diffuse = saturate((facing + wrap) / (1.0 + wrap));
            light += input.albedo * light_color[i].rgb * (falloff * diffuse);
        }
    }
    float3 rgb = texel.rgb * light;
    rgb = lerp(rgb, input.flash.rgb, input.flash.a);
    if (fog_range.z > 0.5)
    {
        const float amount = saturate((input.distance - fog_range.x) * fog_range.y);
        rgb = lerp(rgb, fog_color.rgb, amount);
    }
    return float4(rgb, alpha);
}
