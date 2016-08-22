// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
// http://create.msdn.com/en-US/education/catalog/sample/stock_effects


float ComputeFogFactor(float4 position, uint instId)
{
  return saturate(dot(position, FogVector[instId]));
}


void ApplyFog(inout float4 color, float fogFactor)
{
  color.rgb = lerp(color.rgb, FogColor * color.a, fogFactor);
}


void AddSpecular(inout float4 color, float3 specular)
{
  color.rgb += specular * color.a;
}


struct CommonVSOutput
{
  float4 Pos_ps;
  float4 Diffuse;
  float3 Specular;
  float  FogFactor;
};


CommonVSOutput ComputeCommonVSOutput(float4 position, uint instId)
{
  CommonVSOutput vout;

  vout.Pos_ps = mul(position, WorldViewProj[instId]);
  vout.Diffuse = DiffuseColor;
  vout.Specular = 0;
  vout.FogFactor = ComputeFogFactor(position, instId);

  return vout;
}

#define SetCommonVSOutputParams \
    vout.PositionPS = min16float4(cout.Pos_ps); \
    vout.Diffuse = min16float4(cout.Diffuse); \
    vout.Specular = min16float4(cout.Specular, cout.FogFactor); \
    vout.viewId = idx;

#define SetCommonVSOutputParamsNoFog \
    vout.PositionPS = min16float4(cout.Pos_ps); \
    vout.Diffuse = min16float4(cout.Diffuse);\
    vout.viewId = idx;

#define SetCommonVSOutputParamsVPRT \
    vout.PositionPS = min16float4(cout.Pos_ps); \
    vout.Diffuse = min16float4(cout.Diffuse); \
    vout.Specular = min16float4(cout.Specular, cout.FogFactor); \
    vout.rvtId = idx;

#define SetCommonVSOutputParamsNoFogVPRT \
    vout.PositionPS = min16float4(cout.Pos_ps); \
    vout.Diffuse = min16float4(cout.Diffuse);\
    vout.rvtId = idx;