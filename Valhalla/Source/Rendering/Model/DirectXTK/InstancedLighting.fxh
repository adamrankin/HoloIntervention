// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://create.msdn.com/en-US/education/catalog/sample/stock_effects
//
// Modifications:
//  August 2016, Adam Rankin, Robarts Research Institute
//    - Adding support for instanced rendering (HoloLens)


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
  float3 specular = pow(max(dotH, 0) * zeroL, SpecularPower) * dotL;

  ColorPair result;

  result.Diffuse = mul(diffuse, lightDiffuse)  * DiffuseColor.rgb + EmissiveColor;
  result.Specular = mul(specular, lightSpecular) * SpecularColor;

  return result;
}


CommonVSOutput ComputeCommonVSOutputWithLighting(float4 position, float3 normal, uniform int numLights, uint instId)
{
  CommonVSOutput vout;

  float4 pos_ws = mul(World, position);
  float3 eyeVector = normalize(EyePosition[instId] - pos_ws.xyz);
  float3 worldNormal = normalize(mul(WorldInverseTranspose, normal));

  ColorPair lightResult = ComputeLights(eyeVector, worldNormal, numLights);

  vout.Pos_ps = mul(WorldViewProj[instId], position);
  vout.Diffuse = float4(lightResult.Diffuse, DiffuseColor.a);
  vout.Specular = lightResult.Specular;
  vout.FogFactor = ComputeFogFactor(position, instId);

  return vout;
}


struct CommonVSOutputPixelLighting
{
  float4 Pos_ps;
  float3 Pos_ws;
  float3 Normal_ws;
  float  FogFactor;
};


CommonVSOutputPixelLighting ComputeCommonVSOutputPixelLighting(float4 position, float3 normal, uint instId)
{
  CommonVSOutputPixelLighting vout;

  vout.Pos_ps = mul(WorldViewProj[instId], position);
  vout.Pos_ws = mul(World, position).xyz;
  vout.Normal_ws = normalize(mul(WorldInverseTranspose, normal));
  vout.FogFactor = ComputeFogFactor(position, instId);

  return vout;
}


#define SetCommonVSOutputParamsPixelLighting \
    vout.PositionPS = min16float4(cout.Pos_ps); \
    vout.PositionWS = min16float4(cout.Pos_ws, cout.FogFactor); \
    vout.NormalWS = min16float3(cout.Normal_ws); \
    vout.rvtId = idx;


// Given a local normal, transform it into a tangent space given by surface normal and tangent
float3 PeturbNormal(float3 localNormal, float3 surfaceNormalWS, float3 surfaceTangentWS)
{
  float3 normal = normalize(surfaceNormalWS);
  float3 tangent = normalize(surfaceTangentWS);
  float3 binormal = cross(normal, tangent);     // reconstructed from normal & tangent
  float3x3 tbn = { tangent, binormal, normal }; // world "frame" for local normal

  return mul(localNormal, tbn);               // transform to local to world (tangent space)
}
