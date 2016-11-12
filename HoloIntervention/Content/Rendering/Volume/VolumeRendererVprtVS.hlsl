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
  float3 c_stepSize;
  uint c_numIterations;
  float3 c_scaleFactor;
};

cbuffer ViewProjectionConstantBuffer : register(b1)
{
  float4 cameraPosition;
  float4 lightPosition;
  float4x4 viewProjection[2];
};

struct VertexShaderInput
{
  min16float3 Position : POSITION0;
  uint instId : SV_InstanceID;
};

struct VertexShaderOutput
{
  min16float4 Position : SV_POSITION;
  min16float3 texC : TEXCOORD0;           // Sent to FaceAnalysisPS
  min16float4 pos : TEXCOORD1;            // Send to VolumeRendererPS
  uint rtvId : SV_RenderTargetArrayIndex; // SV_InstanceID % 2
};

VertexShaderOutput main(VertexShaderInput input)
{
  VertexShaderOutput output;
  float4 pos = float4(input.Position * c_scaleFactor, 1.0f);

  int idx = input.instId % 2;

  pos = mul(pos, c_worldPose);
  pos = mul(pos, viewProjection[idx]);
  output.Position = min16float4(pos);
  output.texC = input.Position;
  output.pos = output.Position;
  output.rtvId = idx;

  return output;
}
