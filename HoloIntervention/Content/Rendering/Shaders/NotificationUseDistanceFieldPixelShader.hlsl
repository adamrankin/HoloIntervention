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

// Per-pixel color data passed through to the pixel shader.
struct PixelShaderInput
{
  min16float4 color       : COLOR0;
  min16float2 texCoord    : TEXCOORD1;
  uint        rtvId       : SV_RenderTargetArrayIndex;
  min16float4 pos         : SV_POSITION;
};

Texture2D       tex         : t0;
SamplerState    samp        : s0;

// A smooth step function is used to blend across a one-pixel region.
#define DISTANCE_MAX 0.30f
#define DISTANCE_MIN (DISTANCE_MAX - 0.01f)

// The pixel shader renders a color value based on a bidirectional distance function 
// that is read from a texture.
min16float4 main(PixelShaderInput input) : SV_TARGET
{
    // Read both distance function values.
    min16float2 textureValue = min16float2(tex.Sample(samp, input.texCoord).xy);

    // Clamp the alpha test value to a smooth step in the range [0, 1].
    float2 steppedValues = smoothstep(DISTANCE_MIN, DISTANCE_MAX, textureValue);
    
    // AND the distance function results.
    float multiplier = min(steppedValues.x, steppedValues.y);

    // Apply the result.
    return min16float4(input.color.xyz, 1.f) * multiplier;
}
