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

struct VertexShaderInput
{
  min16float2 pos    : POSITION;
};

struct VertexShaderOutput
{
  min16float4 pos    : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;

  output.pos = min16float4(input.pos, 0.5f, 1.0f);

  return output;
}