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
  min16float4 PositionPS  : SV_Position;
  min16float4 Diffuse     : COLOR0;
  min16float2 TexCoord0   : TEXCOORD0;
  min16float2 TexCoord4   : TEXCOORD4;
  uint instId             : TEXCOORD5;  // SV_InstanceID % 2
};

struct GeometryShaderOutput
{
  min16float4 pos         : SV_POSITION;
  min16float4 Diffuse     : COLOR0;
  min16float2 TexCoord0   : TEXCOORD0;
  min16float2 TexCoord4   : TEXCOORD4;
  uint rtvId              : SV_RenderTargetArrayIndex;
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
        output.TexCoord0 = input[i].TexCoord0;
        output.TexCoord4 = input[i].TexCoord4;
        output.rtvId = input[i].instId;
        outStream.Append(output);
    }
}
