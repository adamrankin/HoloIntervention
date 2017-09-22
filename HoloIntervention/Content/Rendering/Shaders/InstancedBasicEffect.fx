// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://create.msdn.com/en-US/education/catalog/sample/stock_effects


Texture2D<float4> Texture : register(t0);
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

  min16float3 EyePosition[2]           : packoffset(c12);

  min16float3 FogColor                 : packoffset(c14);
  min16float4 FogVector[2]             : packoffset(c15);

  min16float4x4 World                  : packoffset(c17);
  min16float3x3 WorldInverseTranspose  : packoffset(c21);
  min16float4x4 WorldViewProj[2]       : packoffset(c24);
};

// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
  float4        cameraPosition[2];
  float4        lightPosition[2];
  float4x4      view[2];
  float4x4      projection[2];
  float4x4      viewProjection[2];
};

#include "InstancedStructures.fxh"
#include "InstancedCommon.fxh"
#include "InstancedLighting.fxh"

// Vertex shader: basic.
VSOutput VSBasic(VSInput vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParams;

  return vout;
}


// Vertex shader: no fog.
VSOutputNoFog VSBasicNoFog(VSInput vin)
{
  int idx = vin.instId % 2;

  VSOutputNoFog vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParamsNoFog;

  return vout;
}


// Vertex shader: vertex color.
VSOutput VSBasicVc(VSInputVc vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParams;

  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: vertex color, no fog.
VSOutputNoFog VSBasicVcNoFog(VSInputVc vin)
{
  int idx = vin.instId % 2;

  VSOutputNoFog vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParamsNoFog;

  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: texture.
VSOutputTx VSBasicTx(VSInputTx vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Vertex shader: texture, no fog.
VSOutputTxNoFog VSBasicTxNoFog(VSInputTx vin)
{
  int idx = vin.instId % 2;

  VSOutputTxNoFog vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParamsNoFog;

  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Vertex shader: texture + vertex color.
VSOutputTx VSBasicTxVc(VSInputTxVc vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;
  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: texture + vertex color, no fog.
VSOutputTxNoFog VSBasicTxVcNoFog(VSInputTxVc vin)
{
  int idx = vin.instId % 2;

  VSOutputTxNoFog vout;

  CommonVSOutput cout = ComputeCommonVSOutput(vin.Position, idx);
  SetCommonVSOutputParamsNoFog;

  vout.TexCoord = vin.TexCoord;
  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: vertex lighting.
VSOutput VSBasicVertexLighting(VSInputNm vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 3, idx);
  SetCommonVSOutputParams;

  return vout;
}


// Vertex shader: vertex lighting + vertex color.
VSOutput VSBasicVertexLightingVc(VSInputNmVc vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 3, idx);
  SetCommonVSOutputParams;

  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: vertex lighting + texture.
VSOutputTx VSBasicVertexLightingTx(VSInputNmTx vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 3, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Vertex shader: vertex lighting + texture + vertex color.
VSOutputTx VSBasicVertexLightingTxVc(VSInputNmTxVc vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 3, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;
  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: one light.
VSOutput VSBasicOneLight(VSInputNm vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 1, idx);
  SetCommonVSOutputParams;

  return vout;
}


// Vertex shader: one light + vertex color.
VSOutput VSBasicOneLightVc(VSInputNmVc vin)
{
  int idx = vin.instId % 2;

  VSOutput vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 1, idx);
  SetCommonVSOutputParams;

  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: one light + texture.
VSOutputTx VSBasicOneLightTx(VSInputNmTx vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 1, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Vertex shader: one light + texture + vertex color.
VSOutputTx VSBasicOneLightTxVc(VSInputNmTxVc vin)
{
  int idx = vin.instId % 2;

  VSOutputTx vout;

  CommonVSOutput cout = ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 1, idx);
  SetCommonVSOutputParams;

  vout.TexCoord = vin.TexCoord;
  vout.Diffuse *= vin.Color;

  return vout;
}


// Vertex shader: pixel lighting.
VSOutputPixelLighting VSBasicPixelLighting(VSInputNm vin)
{
  int idx = vin.instId % 2;

  VSOutputPixelLighting vout;

  CommonVSOutputPixelLighting cout = ComputeCommonVSOutputPixelLighting(vin.Position, vin.Normal, idx);
  SetCommonVSOutputParamsPixelLighting;

  vout.Diffuse = float4(1, 1, 1, DiffuseColor.a);

  return vout;
}


// Vertex shader: pixel lighting + vertex color.
VSOutputPixelLighting VSBasicPixelLightingVc(VSInputNmVc vin)
{
  int idx = vin.instId % 2;

  VSOutputPixelLighting vout;

  CommonVSOutputPixelLighting cout = ComputeCommonVSOutputPixelLighting(vin.Position, vin.Normal, idx);
  SetCommonVSOutputParamsPixelLighting;

  vout.Diffuse.rgb = vin.Color.rgb;
  vout.Diffuse.a = vin.Color.a * DiffuseColor.a;

  return vout;
}


// Vertex shader: pixel lighting + texture.
VSOutputPixelLightingTx VSBasicPixelLightingTx(VSInputNmTx vin)
{
  int idx = vin.instId % 2;

  VSOutputPixelLightingTx vout;

  CommonVSOutputPixelLighting cout = ComputeCommonVSOutputPixelLighting(vin.Position, vin.Normal, idx);
  SetCommonVSOutputParamsPixelLighting;

  vout.Diffuse = float4(1, 1, 1, DiffuseColor.a);
  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Vertex shader: pixel lighting + texture + vertex color.
VSOutputPixelLightingTx VSBasicPixelLightingTxVc(VSInputNmTxVc vin)
{
  int idx = vin.instId % 2;

  VSOutputPixelLightingTx vout;

  CommonVSOutputPixelLighting cout = ComputeCommonVSOutputPixelLighting(vin.Position, vin.Normal, idx);
  SetCommonVSOutputParamsPixelLighting;

  vout.Diffuse.rgb = vin.Color.rgb;
  vout.Diffuse.a = vin.Color.a * DiffuseColor.a;
  vout.TexCoord = vin.TexCoord;

  return vout;
}


// Pixel shader: basic.
float4 PSBasic(PSInput pin) : SV_Target0
{
  float4 color = pin.Diffuse;

  ApplyFog(color, pin.Specular.w);

  return color;
}


// Pixel shader: no fog.
float4 PSBasicNoFog(PSInputNoFog pin) : SV_Target0
{
  return pin.Diffuse;
}


// Pixel shader: texture.
float4 PSBasicTx(PSInputTx pin) : SV_Target0
{
  float4 color = Texture.Sample(Sampler, pin.TexCoord) * pin.Diffuse;

  ApplyFog(color, pin.Specular.w);

  return color;
}


// Pixel shader: texture, no fog.
float4 PSBasicTxNoFog(PSInputTxNoFog pin) : SV_Target0
{
  return Texture.Sample(Sampler, pin.TexCoord) * pin.Diffuse;
}


// Pixel shader: vertex lighting.
float4 PSBasicVertexLighting(PSInput pin) : SV_Target0
{
  float4 color = pin.Diffuse;

  AddSpecular(color, pin.Specular.rgb);
  ApplyFog(color, pin.Specular.w);

  return color;
}


// Pixel shader: vertex lighting, no fog.
float4 PSBasicVertexLightingNoFog(PSInput pin) : SV_Target0
{
  float4 color = pin.Diffuse;

  AddSpecular(color, pin.Specular.rgb);

  return color;
}


// Pixel shader: vertex lighting + texture.
float4 PSBasicVertexLightingTx(PSInputTx pin) : SV_Target0
{
  float4 color = Texture.Sample(Sampler, pin.TexCoord) * pin.Diffuse;

  AddSpecular(color, pin.Specular.rgb);
  ApplyFog(color, pin.Specular.w);

  return color;
}


// Pixel shader: vertex lighting + texture, no fog.
float4 PSBasicVertexLightingTxNoFog(PSInputTx pin) : SV_Target0
{
  float4 color = Texture.Sample(Sampler, pin.TexCoord) * pin.Diffuse;

  AddSpecular(color, pin.Specular.rgb);

  return color;
}


// Pixel shader: pixel lighting.
float4 PSBasicPixelLighting(PSInputPixelLighting pin) : SV_Target0
{
  float4 color = pin.Diffuse;

  float3 eyeVector = normalize(EyePosition[pin.rvt] - pin.PositionWS.xyz);
  float3 worldNormal = normalize(pin.NormalWS);

  ColorPair lightResult = ComputeLights(eyeVector, worldNormal, 3);

  color.rgb *= lightResult.Diffuse;

  AddSpecular(color, lightResult.Specular);
  ApplyFog(color, pin.PositionWS.w);

  return color;
}


// Pixel shader: pixel lighting + texture.
float4 PSBasicPixelLightingTx(PSInputPixelLightingTx pin) : SV_Target0
{
  float4 color = Texture.Sample(Sampler, pin.TexCoord) * pin.Diffuse;

  float3 eyeVector = normalize(EyePosition[pin.rvt] - pin.PositionWS.xyz);
  float3 worldNormal = normalize(pin.NormalWS);

  ColorPair lightResult = ComputeLights(eyeVector, worldNormal, 3);

  color.rgb *= lightResult.Diffuse;

  AddSpecular(color, lightResult.Specular);
  ApplyFog(color, pin.PositionWS.w);

  return color;
}
