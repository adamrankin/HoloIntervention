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

struct VertexShaderOutput
{
  float4 PositionPS : SV_Position;
  float4 Diffuse    : COLOR0;
  float4 Specular   : COLOR1;
  uint rtvId        : SV_RenderTargetArrayIndex; // SV_InstanceID % 2
};

struct VertexShaderInput
{
  float4 Position : SV_Position;
  float3 Normal   : NORMAL0;
  float4 Tangent  : TANGENT0;
  float4 Color    : COLOR0;
  float2 TexCoord : TEXCOORD0;
  uint instId     : SV_InstanceID;
};

struct ColorPair
{
  float3 Diffuse;
  float3 Specular;
};

ColorPair ComputeLights(float3 eyeVector, float3 worldNormal, uniform int numLights)
{
  float3x3 lightDirections = 0;
  float3x3 lightDiffuse = 0;
  float3x3 lightSpecular = 0;
  float3x3 halfVectors = 0;

  [unroll]
  for (int i = 0; i < numLights; i++)
  {
    lightDirections[i] = LightDirection[i];
    lightDiffuse[i] = LightDiffuseColor[i];
    lightSpecular[i] = LightSpecularColor[i];

    halfVectors[i] = normalize(eyeVector - lightDirections[i]);
  }

  float3 dotL = mul(-lightDirections, worldNormal);
  float3 dotH = mul(halfVectors, worldNormal);

  float3 zeroL = step(0, dotL);

  float3 diffuse = zeroL * dotL;
  float3 specular = pow(max(dotH, 0) * zeroL, SpecularPower);

  ColorPair result;

  result.Diffuse = mul(diffuse, lightDiffuse)  * DiffuseColor.rgb + EmissiveColor;
  result.Specular = mul(specular, lightSpecular) * SpecularColor;

  return result;
}

VertexShaderOutput main(VertexShaderInput vin)
{
  VertexShaderOutput vout;

  int idx = vin.instId % 2;

  float4 pos_ws = mul(vin.Position, World);
  float3 eyeVector = normalize(EyePosition[idx].xyz - pos_ws.xyz);
  float3 worldNormal = normalize(mul(vin.Normal, WorldInverseTranspose));

  ColorPair lightResult = ComputeLights(eyeVector, worldNormal, 1);

  float4x4 worldViewProj = mul(ViewProjection[idx], World);

  vout.PositionPS = mul(vin.Position, worldViewProj);
  vout.Diffuse = float4(lightResult.Diffuse, DiffuseColor.a);
  vout.Specular = float4(lightResult.Specular, 1);

  vout.Diffuse *= vin.Color;

  // Set the render target array index.
  vout.rtvId = idx;

  return vout;
}