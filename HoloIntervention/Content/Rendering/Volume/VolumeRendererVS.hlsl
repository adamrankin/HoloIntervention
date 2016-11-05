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
cbuffer VolumeConstantBuffer : register(b0)
{
  float4x4 worldPose;
  float maximumXValue;
  float padding[3];
};

// A constant buffer that stores each set of view and projection matrices in column-major format.
cbuffer ViewProjectionConstantBuffer : register(b1)
{
  float4 cameraPosition;
  float4 lightPosition;
  float4x4 viewProjection[2];
};

// Per-vertex data used as input to the vertex shader.
struct VertexShaderInput
{
  min16float3 pos : POSITION0;
  uint instId : SV_InstanceID;
};

// Per-vertex data passed to the geometry shader.
// Note that the render target array index will be set by the geometry shader using the value of viewId.
struct VertexShaderOutput
{
  min16float4 pos : SV_POSITION;
  uint viewId : TEXCOORD5; // SV_InstanceID % 2
};

// Simple shader to do vertex processing on the GPU.
VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;
  float4 pos = float4(input.pos, 1.0f);

  int idx = input.instId % 2;

  pos = mul(pos, worldPose);
  pos = mul(pos, viewProjection[idx]);
  output.pos = min16float4(pos);

  // Set the instance ID. The pass-through geometry shader will set the
  // render target array index to whatever value is set here.
  output.viewId = idx;

  return output;
}