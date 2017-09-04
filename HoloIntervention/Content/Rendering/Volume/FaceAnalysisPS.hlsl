/*====================================================================
Copyright(c) 2017 Adam Rankin


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

struct PixelShaderInput
{
  min16float4 Position              : SV_POSITION;
  min16float3 ModelSpacePosition    : TEXCOORD0;
  uint        rtvId                 : SV_RenderTargetArrayIndex;
};

float4 main(PixelShaderInput input) : SV_TARGET
{
    // Account for RHS x right, y up, z towards -> x right, y down, z away
    //   Flip y, z
    //   Since we know this is a unit cube, 1 - value = flip
  return float4(input.ModelSpacePosition.x, 1.f - input.ModelSpacePosition.y, 1.f - input.ModelSpacePosition.z, 1.0f);
}