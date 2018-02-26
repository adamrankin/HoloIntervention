/*====================================================================
Copyright(c) 2018 Adam Rankin


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
  float4x4  c_worldPose           : packoffset(c0);
  float3    c_stepSize            : packoffset(c4);
  float     c_tfMaximumXValue     : packoffset(c4.w);
  float2    c_viewportDimensions  : packoffset(c5);
  uint      c_tfArraySize         : packoffset(c5.z);
  uint      c_numIterations       : packoffset(c5.w);
};

struct PixelShaderInput
{
  min16float4 Position              : SV_POSITION;
  min16float3 ModelSpacePosition    : TEXCOORD0; // not used
  uint        rtvId                 : SV_RenderTargetArrayIndex;
};

struct LookupTableBufferType
{
  float4 lookupValue;
};
StructuredBuffer<LookupTableBufferType> r_opacityLookupTable      : register(t0);
Texture3D                               r_volumeTexture           : register(t1);
Texture2DArray                          r_frontPositionTextures   : register(t2);
Texture2DArray                          r_backPositionTextures    : register(t3);
SamplerState                            r_sampler                 : s0;

float4 main(PixelShaderInput input) : SV_TARGET
{
  float3 pixelPosition = float3((input.Position.xy - float2(0.5f, 0.5f)) / c_viewportDimensions, input.rtvId);
  float3 front = r_frontPositionTextures.SampleLevel(r_sampler, pixelPosition, 0.f).xyz;
  float3 back = r_backPositionTextures.SampleLevel(r_sampler, pixelPosition, 0.f).xyz;
    
  float3 dir = normalize(back - front);
  float3 pos = front;
  float4 dst = float4(0, 0, 0, 0);
  float4 src;
	float3 step = dir * c_stepSize.xyz;
    
  [loop]
  for (uint i = 0; i < c_numIterations; i++)
  {
    // Break if the position is greater than <1, 1, 1>
    if(pos.x > 1.0f || pos.y > 1.0f || pos.z > 1.0f)
    {
      break;
    }

    src = r_volumeTexture.SampleLevel(r_sampler, pos, 0.f);
    //src = float4(1.f, 0.f, 0.f, 1.f);
    //float ratio = src.r * 255 / c_tfMaximumXValue;
    //float arrayIndex = ratio * c_tfArraySize;
    //float lowerIndex = floor(arrayIndex);
    //float upperIndex = ceil(arrayIndex);
    //float opacity = (r_opacityLookupTable[floor(arrayIndex)].lookupValue * (arrayIndex % 1.f) + r_opacityLookupTable[ceil(arrayIndex)].lookupValue * (1.f - (arrayIndex % 1.f))).a;

    src.a *= 0.1;

		// Front to back blending
		//  dst.rgb = dst.rgb + (1 - dst.a) * src.a * src.rgb
		//  dst.a   = dst.a   + (1 - dst.a) * src.a		
    src.rgb *= src.a;
    dst += (1.0f - dst.a) * src;
		
		// Break from the loop when alpha gets high enough
    if(dst.a >= .95f)
      break;
		
		// Advance the current position
    pos.xyz += step;
  }

  dst.y = dst.z = dst.x;
  return dst;
}