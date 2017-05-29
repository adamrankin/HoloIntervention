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

cbuffer SliceConstantBuffer : register(b0)
{
  float4x4  model;
  float4    blackMapColour;
  float4    whiteMinusBlackColour;
};

// Per-pixel color data passed through to the pixel shader.
struct PixelShaderInput
{
  min16float4 pos       : SV_POSITION;
  min16float2 texCoord  : TEXCOORD1;
  uint        rtvId     : SV_RenderTargetArrayIndex;
};

Texture2D     tex             : register(t0);
SamplerState  textureSampler  : s0;

// The pixel shader renders a color value sampled from a texture
min16float4 main(PixelShaderInput input) : SV_TARGET
{
  float4 sample = tex.Sample(textureSampler, input.texCoord);
  float4 result = blackMapColour + (whiteMinusBlackColour * sample);

  return min16float4(result.r, result.g, result.b, result.w);
}