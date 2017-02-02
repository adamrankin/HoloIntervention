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

// A constant buffer that stores the model transform.
cbuffer SliceConstantBuffer : register(b0)
{
  float4x4      model;
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
  min16float3   pos      : POSITION0;
  min16float2   texCoord : TEXCOORD1;
  uint          instId   : SV_InstanceID;
};

// Per-vertex data passed to the geometry shader.
// Note that the render target array index is set here in the vertex shader.
struct VertexShaderOutput
{
  min16float4   pos      : SV_POSITION;
  min16float2   texCoord : TEXCOORD1;
  uint          rtvId    : SV_RenderTargetArrayIndex; // SV_InstanceID % 2
};

// Simple shader to do vertex processing on the GPU.
VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;
  float4 pos = float4(input.pos, 1.0f);

  int idx = input.instId % 2;

  pos = mul(model, pos);
  pos = mul(viewProjection[idx], pos);
  output.pos = min16float4(pos);

  // Pass through the texture coordinates without modification.
  output.texCoord = input.texCoord;

  // Set the render target array index.
  output.rtvId = idx;

  return output;
}
