//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// A constant buffer that stores per-mesh data.
cbuffer ModelConstantBuffer : register(b0)
{
  float4x4      modelToWorld;
  min16float4x4 normalToWorld;
  min16float4   colorFadeFactor;
};

// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
  float4    cameraPosition[2];
  float4    lightPosition[2];
  float4x4  view[2];
  float4x4  projection[2];
  float4x4  viewProjection[2];
};

// Per-vertex data used as input to the vertex shader.
struct VertexShaderInput
{
  min16float3 pos     : POSITION;
  min16float3 norm    : NORMAL0;
  uint        instId  : SV_InstanceID;
};

// Per-vertex data passed to the geometry shader.
// Note that the render target array index will be set by the geometry shader
// using the value of viewId.
struct VertexShaderOutput
{
  min16float4 screenPos   : SV_POSITION;
  min16float3 worldPos    : POSITION0;
  min16float3 worldNorm   : NORMAL0;
  min16float3 color       : COLOR0;
  uint        viewId      : TEXCOORD9;  // SV_InstanceID % 2
};

// Simple shader to do vertex processing on the GPU.
VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;
  float4 pos = float4(input.pos, 1.0f);

  int idx = input.instId % 2;

  // Transform the vertex position into world space.
  pos = mul(pos, modelToWorld);

  // Store the world position.
  output.worldPos = (min16float3)pos;

  // Correct for perspective and project the vertex position onto the screen.
  pos = mul(viewProjection[idx], pos);
  output.screenPos = (min16float4)pos;

  // Pass a color.
  output.color = min16float3(1.f, 1.f, 1.f);

  // Set the instance ID. The pass-through geometry shader will set the
  // render target array index to whatever value is set here.
  output.viewId = idx;

  // Compute the normal.
  min16float4 normalVector = min16float4(input.norm, min16float(0.f));
  output.worldNorm = normalize((min16float3)mul(normalVector, normalToWorld));

  return output;
}