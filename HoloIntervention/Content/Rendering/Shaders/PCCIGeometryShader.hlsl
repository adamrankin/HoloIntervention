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

//Per-vertex data from the vertex shader.
struct GeometryShaderInput
{
  min16float4 Diffuse     : COLOR0;
  min16float4 Specular    : COLOR1;
  min16float4 PositionPS  : SV_Position;
  uint instId             : TEXCOORD5;  // SV_InstanceID % 2
};

struct GeometryShaderOutput
{
  min16float4 Diffuse     : COLOR0;
  min16float4 Specular    : COLOR1;
  uint rtvId              : SV_RenderTargetArrayIndex;
  min16float4 pos         : SV_POSITION;
};

// This geometry shader is a pass-through that leaves the geometry unmodified 
// and sets the render target array index.
[maxvertexcount(3)]
void main(triangle GeometryShaderInput input[3], inout TriangleStream<GeometryShaderOutput> outStream)
{
    GeometryShaderOutput output;
    [unroll(3)]
    for (int i = 0; i < 3; ++i)
    {
        output.pos   = input[i].PositionPS;
        output.Diffuse = input[i].Diffuse;
        output.Specular = input[i].Specular;
        output.rtvId = input[i].instId;
        outStream.Append(output);
    }
}
