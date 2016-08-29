// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://create.msdn.com/en-US/education/catalog/sample/stock_effects


// Vertex shader input structures.

struct VSInput
{
  min16float4 Position : SV_Position;
  uint instId : SV_InstanceID;
};

struct VSInputVc
{
  min16float4 Position : SV_Position;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputTx
{
  min16float4 Position : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  uint instId : SV_InstanceID;
};

struct VSInputTxVc
{
  min16float4 Position : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputNm
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  uint instId : SV_InstanceID;
};

struct VSInputNmVc
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputNmTx
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float2 TexCoord : TEXCOORD0;
  uint instId : SV_InstanceID;
};

struct VSInputNmTxVc
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputNmTxTangent
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float4 Tangent : TANGENT;
  min16float2 TexCoord : TEXCOORD0;
  uint instId : SV_InstanceID;
};

struct VSInputNmTxVcTangent
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float4 Tangent : TANGENT;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputTx2
{
  min16float4 Position : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
  uint instId : SV_InstanceID;
};

struct VSInputTx2Vc
{
  min16float4 Position : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
  min16float4 Color : COLOR;
  uint instId : SV_InstanceID;
};

struct VSInputNmTxWeights
{
  min16float4 Position : SV_Position;
  min16float3 Normal : NORMAL;
  min16float2 TexCoord : TEXCOORD0;
  uint4  Indices : BLENDINDICES0;
  min16float4 Weights : BLENDWEIGHT0;
  uint instId : SV_InstanceID;
};



// Vertex shader output structures.

struct VSOutput
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputNoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputTx
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputTxNoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float2 TexCoord : TEXCOORD1;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputPixelLighting
{
  min16float4 PositionPS : SV_Position;
  min16float4 PositionWS : TEXCOORD0;
  min16float3 NormalWS : TEXCOORD4;
  min16float4 Diffuse : COLOR0;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputPixelLightingTx
{
  min16float4 PositionPS : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 PositionWS : TEXCOORD1;
  min16float3 NormalWS : TEXCOORD2;
  min16float4 Diffuse : COLOR0;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputPixelLightingTxTangent
{
  min16float4 PositionPS : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 PositionWS : TEXCOORD1;
  min16float3 NormalWS : TEXCOORD2;
  min16float3 TangentWS : TEXCOORD3;
  min16float4 Diffuse : COLOR0;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputTx2
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputTx2NoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};

struct VSOutputTxEnvMap
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
  min16float3 EnvCoord : TEXCOORD1;
#if defined(USE_VPRT)
  uint rvtId : SV_RenderTargetArrayIndex;
#else
  uint viewId : TEXCOORD5;
#endif
};



// Pixel shader input structures.

struct PSInput
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputNoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputTx
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputTxNoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float2 TexCoord : TEXCOORD0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputPixelLighting
{
  min16float4 PositionPS : SV_Position;
  min16float4 PositionWS : TEXCOORD0;
  min16float3 NormalWS : TEXCOORD1;
  min16float4 Diffuse : COLOR0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputPixelLightingTx
{
  min16float4 PositionPS : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 PositionWS : TEXCOORD1;
  min16float3 NormalWS : TEXCOORD2;
  min16float4 Diffuse : COLOR0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputPixelLightingTxTangent
{
  min16float4 PositionPS : SV_Position;
  min16float2 TexCoord : TEXCOORD0;
  min16float4 PositionWS : TEXCOORD1;
  min16float3 NormalWS : TEXCOORD2;
  min16float3 TangentWS : TEXCOORD3;
  min16float4 Diffuse : COLOR0;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputTx2
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputTx2NoFog
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float2 TexCoord : TEXCOORD0;
  min16float2 TexCoord2 : TEXCOORD1;
  uint rvt : SV_RenderTargetArrayIndex;
};

struct PSInputTxEnvMap
{
  min16float4 PositionPS : SV_Position;
  min16float4 Diffuse : COLOR0;
  min16float4 Specular : COLOR1;
  min16float2 TexCoord : TEXCOORD0;
  min16float3 EnvCoord : TEXCOORD1;
  uint rvt : SV_RenderTargetArrayIndex;
};