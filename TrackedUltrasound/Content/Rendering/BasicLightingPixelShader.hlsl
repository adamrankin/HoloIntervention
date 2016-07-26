Texture2D<float4> Texture : register(t0);
sampler Sampler : register(s0);

cbuffer Parameters : register(b0)
{
  float4 DiffuseColor             : packoffset(c0);
  float3 EmissiveColor            : packoffset(c1);
  float3 SpecularColor            : packoffset(c2);
  float  SpecularPower : packoffset(c2.w);

  float3 LightDirection[3]        : packoffset(c3);
  float3 LightDiffuseColor[3]     : packoffset(c6);
  float3 LightSpecularColor[3]    : packoffset(c9);

  float3 EyePosition[2]           : packoffset(c12);

  float3 FogColor                 : packoffset(c14);
  float4 FogVector                : packoffset(c15);

  float4x4 World                  : packoffset(c16);
  float3x3 WorldInverseTranspose  : packoffset(c20);
  float4x4 WorldViewProj[2]       : packoffset(c23);
};

struct PSInput
{
  min16float4 pos : SV_POSITION;
  float4 Diffuse  : COLOR0;
  float4 Specular : COLOR1;
};

// Pixel shader: vertex lighting.
float4 main(PSInput pin) : SV_Target0
{
  float4 color = pin.Diffuse;

  color.rgb += pin.Specular.rgb * color.a;
  color.rgb = lerp(color.rgb, FogColor * color.a, pin.Specular.w);

  return color;
}