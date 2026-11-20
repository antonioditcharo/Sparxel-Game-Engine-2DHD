// Sprite vertex shader: places quad corners on screen and passes texture coordinates, tint
// and effects through. Bindings follow the SDL GPU rules for vertex shaders: uniform
// buffers in (b, space1).

cbuffer Camera : register(b0, space1)
{
    // clip.xy = position.xy * transform.xy + transform.zw
    float4 transform;
};

struct Input
{
    float2 position : TEXCOORD0; // pixels
    float2 uv : TEXCOORD1;
    float4 tint : TEXCOORD2;
    float4 flash : TEXCOORD3;   // rgb, and how much
    float4 outline : TEXCOORD4; // rgb, and its alpha (0: none)
    float4 frame : TEXCOORD5;   // the source's texture coordinates: left, top, right, bottom
    float4 params : TEXCOORD6;  // x: dissolve, yz: one texel
};

struct Output
{
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
    float4 flash : TEXCOORD2;
    float4 outline : TEXCOORD3;
    float4 frame : TEXCOORD4;
    float4 params : TEXCOORD5;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position * transform.xy + transform.zw, 0.0, 1.0);
    output.uv = input.uv;
    output.tint = input.tint;
    output.flash = input.flash;
    output.outline = input.outline;
    output.frame = input.frame;
    output.params = input.params;
    return output;
}
