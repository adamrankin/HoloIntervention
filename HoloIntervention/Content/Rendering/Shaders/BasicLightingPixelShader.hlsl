Texture2D<float4> Texture : register(t0);
sampler Sampler : register(s0);

cbuffer Parameters : register(b0)
{
  float4 DiffuseColor             : packoffset(c0);
  float3 EmissiveColor            : packoffset(c1);
  float3 SpecularColor            : packoffset(c2);
  float  SpecularPower            : packoffset(c2.w);

  float3 LightDirection[3]        : packoffset(c3);
  float3 LightDiffuseColor[3]     : packoffset(c6);
  float3 LightSpecularColor[3]    : packoffset(c9);

  float4x4 World                  : packoffset(c12);
  float3x3 WorldInverseTranspose  : packoffset(c16);
};

// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
  float4 EyePosition[2];
  float4x4 ViewProjection[2];
};

struct PSInput
{
  min16float4 pos : SV_POSITION;
  min16float4 Diffuse  : COLOR0;
  min16float4 Specular : COLOR1;
};

// Pixel shader: vertex lighting.
float4 main(PSInput pin) : SV_Target0
{
  min16float4 color = pin.Diffuse;

  color.rgb += pin.Specular.rgb * color.a;
  color.rgb = lerp(color.rgb, color.a, pin.Specular.w);

  return color;
}