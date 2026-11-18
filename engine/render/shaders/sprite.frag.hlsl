// Sprite pixel shader: the texel times the tint, with the sprite's effects (render::
// SpriteEffects): an outline on clear texels beside opaque ones, a flash of colour mixed
// over it, and dissolving, texel by texel. Bindings follow the SDL GPU rules for fragment
// shaders: sampled textures and samplers in (t, space2) and (s, space2).

Texture2D<float4> sprite_texture : register(t0, space2);
SamplerState sprite_sampler : register(s0, space2);

struct Input
{
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
    float4 flash : TEXCOORD2;
    float4 outline : TEXCOORD3;
    float4 frame : TEXCOORD4;
    float4 params : TEXCOORD5;
};

// The texel at `uv`, clear outside the source (the quad may reach a texel beyond it).
float4 texel_at(float2 uv, float4 frame)
{
    if (uv.x < frame.x || uv.y < frame.y || uv.x > frame.z || uv.y > frame.w)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }
    return sprite_texture.SampleLevel(sprite_sampler, uv, 0.0);
}

// A number from 0 to 1 for a texel, the same every frame.
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
    // Only an outlined quad reaches past its source: elsewhere texels are sampled as they
    // are.
    const bool bounded = input.outline.a > 0.0;
    float4 color = (bounded ? texel_at(input.uv, input.frame)
                            : sprite_texture.Sample(sprite_sampler, input.uv)) *
                   input.tint;
    if (input.outline.a > 0.0 && color.a < 0.5 * input.tint.a)
    {
        const float4 f = input.frame;
        const float near = max(max(texel_at(input.uv + float2(texel_size.x, 0.0), f).a,
                                   texel_at(input.uv - float2(texel_size.x, 0.0), f).a),
                               max(texel_at(input.uv + float2(0.0, texel_size.y), f).a,
                                   texel_at(input.uv - float2(0.0, texel_size.y), f).a));
        if (near >= 0.5)
        {
            color = float4(input.outline.rgb, input.outline.a * input.tint.a);
        }
    }
    color.rgb = lerp(color.rgb, input.flash.rgb, input.flash.a);
    return color;
}
