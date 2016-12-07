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
  float4x4		c_worldPose;
	float				c_maximumXValue; // used for transfer function logic
	float3			c_stepSize;
  float2      c_viewportDimensions;
  uint        c_numIterations;
};

struct PixelShaderInput
{
  min16float4 Position              : SV_POSITION;
  min16float3 ModelSpacePosition    : TEXCOORD0; // not used
  uint				rtvId                 : SV_RenderTargetArrayIndex;
};

// Must match ITransferFunction::TRANSFER_FUNCTION_TABLE_SIZE
#define TRANSFER_FUNCTION_TABLE_SIZE 1024

ByteAddressBuffer r_lookupTable             : register(t0);
Texture3D         r_volumeTexture           : register(t1);
Texture2DArray    r_frontPositionTextures   : register(t2);
Texture2DArray    r_backPositionTextures    : register(t3);
SamplerState      r_sampler                 : s0;

float4 main(PixelShaderInput input) : SV_TARGET
{
  float3 pixelPosition = float3((input.Position.xy - float2(0.5f, 0.5f)) / c_viewportDimensions, input.rtvId);
  float3 front = r_frontPositionTextures.SampleLevel(r_sampler, pixelPosition, 0.f).xyz;
  float3 back = r_backPositionTextures.SampleLevel(r_sampler, pixelPosition, 0.f).xyz;
    
  float3 dir = normalize(back - front);
  float3 pos = front;
  float4 dst = float4(0, 0, 0, 0);
  float4 src;
	float3 step = dir * c_stepSize;
    
  if( pos.x != 256.f)
  {
    dst = float4(front + back, 1.f);
    dst += float4(1.f, 0.f, 0.f, 1.f);
  }

  if( dir.x == 256.f )
  {
    dst += float4(0.f, 0.f, 0.f, 0.f);
  }
    /*
  [loop]
  for (uint i = 0; i < c_numIterations; i++)
  {
    src = r_volumeTexture.SampleLevel(r_sampler, pos, 0.f);
    src.a *= .1f; // Reduce the alpha to have a more transparent result
									//  this needs to be adjusted based on the step size
									//  i.e. the more steps we take, the faster the alpha will grow	

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
		
		// Break if the position is greater than <1, 1, 1>
    if(pos.x > 1.0f || pos.y > 1.0f || pos.z > 1.0f)
    {
      break;
    }
  }
*/

  return dst;
}