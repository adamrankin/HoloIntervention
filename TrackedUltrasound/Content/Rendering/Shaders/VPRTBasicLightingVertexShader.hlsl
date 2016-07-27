Texture2D<min16float4> Texture : register(t0);
sampler Sampler : register(s0);

cbuffer Parameters : register(b0)
{
  min16float4 DiffuseColor             : packoffset(c0);
  min16float3 EmissiveColor            : packoffset(c1);
  min16float3 SpecularColor            : packoffset(c2);
  min16float  SpecularPower            : packoffset(c2.w);

  min16float3 LightDirection[3]        : packoffset(c3);
  min16float3 LightDiffuseColor[3]     : packoffset(c6);
  min16float3 LightSpecularColor[3]    : packoffset(c9);

  min16float4x4 World                  : packoffset(c12);
  min16float3x3 WorldInverseTranspose  : packoffset(c16);
};

// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
  min16float4 EyePosition[2];
  min16float4x4 ViewProjection[2];
};

struct VertexShaderOutput
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse    : COLOR0;
  min16float4 Specular   : COLOR1;
  uint rtvId        : SV_RenderTargetArrayIndex; // SV_InstanceID % 2
};

struct VertexShaderInput
{
  min16float4 Position : SV_Position;
  min16float3 Normal   : NORMAL0;
  min16float4 Tangent  : TANGENT0;
  min16float4 Color    : COLOR0;
  min16float2 TexCoord : TEXCOORD0;
  uint instId     : SV_InstanceID;
};

struct ColorPair
{
  min16float3 Diffuse;
  min16float3 Specular;
};

ColorPair ComputeLights(min16float3 eyeVector, min16float3 worldNormal, uniform int numLights)
{
  min16float3x3 lightDirections = 0;
  min16float3x3 lightDiffuse = 0;
  min16float3x3 lightSpecular = 0;
  min16float3x3 halfVectors = 0;

  [unroll]
  for (int i = 0; i < numLights; i++)
  {
    lightDirections[i] = LightDirection[i];
    lightDiffuse[i] = LightDiffuseColor[i];
    lightSpecular[i] = LightSpecularColor[i];

    halfVectors[i] = normalize(eyeVector - lightDirections[i]);
  }

  min16float3 dotL = mul(-lightDirections, worldNormal);
  min16float3 dotH = mul(halfVectors, worldNormal);

  min16float3 zeroL = step(0, dotL);

  min16float3 diffuse = zeroL * dotL;
  min16float3 specular = pow(max(dotH, 0) * zeroL, SpecularPower);

  ColorPair result;

  result.Diffuse = mul(diffuse, lightDiffuse)  * DiffuseColor.rgb + EmissiveColor;
  result.Specular = mul(specular, lightSpecular) * SpecularColor;

  return result;
}

VertexShaderOutput main(VertexShaderInput vin)
{
  VertexShaderOutput vout;

  int idx = vin.instId % 2;

  min16float4 pos_ws = mul(vin.Position, World);
  min16float3 eyeVector = normalize(EyePosition[idx].xyz - pos_ws.xyz);
  min16float3 worldNormal = normalize(mul(vin.Normal, WorldInverseTranspose));

  ColorPair lightResult = ComputeLights(eyeVector, worldNormal, 1);

  min16float4x4 worldViewProj = mul(ViewProjection[idx], World);

  vout.PositionPS = mul(vin.Position, worldViewProj);
  vout.Diffuse = min16float4(lightResult.Diffuse, DiffuseColor.a);
  vout.Specular = min16float4(lightResult.Specular, 1);

  vout.Diffuse *= vin.Color;

  // Set the render target array index.
  vout.rtvId = idx;

  return vout;
}