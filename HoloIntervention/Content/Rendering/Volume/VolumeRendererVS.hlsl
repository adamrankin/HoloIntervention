/*====================================================================
Copyright(c) 2016 Adam Rankin


Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files(the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and / or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
====================================================================*/

cbuffer VolumeConstantBuffer : register(b0)
{
  float4x4 c_worldPose;
  float c_maximumXValue;
  uint c_tfArraySize;
  float3 c_stepSize;
  uint c_numIterations;
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

cbuffer VolumeRendererConstantBuffer : register(b2)
{
  float4 viewportDimensions;
};

struct VertexShaderInput
{
  min16float3 Position  : POSITION0;
  uint        instId    : SV_InstanceID;
};

struct VertexShaderOutput
{
  min16float4 Position              : SV_POSITION;
  min16float3 ModelSpacePosition    : TEXCOORD0; // used in FaceAnalysisPS
  uint        viewId                : TEXCOORD5; // SV_InstanceID % 2
};

VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;

  int idx = input.instId % 2;
  
  float4 pos = mul(c_worldPose, float4(input.Position, 1.f));
  pos = mul(viewProjection[idx], pos);
  output.Position = min16float4(pos);
  output.ModelSpacePosition = input.Position;
  output.viewId = idx;

  return output;
}
