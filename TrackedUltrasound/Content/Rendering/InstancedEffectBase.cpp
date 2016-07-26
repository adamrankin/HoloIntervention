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

#include "pch.h"
#include "InstancedEffectBase.h"

namespace DirectX
{
  // Constructor initializes default light settings.
  EffectStereoLights::EffectStereoLights()
  {
    emissiveColor = g_XMZero;
    ambientLightColor = g_XMZero;

    for ( int i = 0; i < MaxDirectionalLights; i++ )
    {
      lightEnabled[i] = ( i == 0 );
      lightDiffuseColor[i] = g_XMOne;
      lightSpecularColor[i] = g_XMZero;
    }
  }


#pragma prefast(push)
#pragma prefast(disable:22103, "PREFAST doesn't understand buffer is bounded by a static const value even with SAL" )

  // Initializes constant buffer fields to match the current lighting state.
  _Use_decl_annotations_ void EffectStereoLights::InitializeConstants( XMVECTOR& specularColorAndPowerConstant,
      XMVECTOR* lightDirectionConstant,
      XMVECTOR* lightDiffuseConstant,
      XMVECTOR* lightSpecularConstant )
  {
    static const XMVECTORF32 defaultSpecular = { 1, 1, 1, 16 };
    static const XMVECTORF32 defaultLightDirection = { 0, -1, 0, 0 };

    specularColorAndPowerConstant = defaultSpecular;

    for ( int i = 0; i < MaxDirectionalLights; i++ )
    {
      lightDirectionConstant[i] = defaultLightDirection;

      lightDiffuseConstant[i] = lightEnabled[i] ? lightDiffuseColor[i] : g_XMZero;
      lightSpecularConstant[i] = lightEnabled[i] ? lightSpecularColor[i] : g_XMZero;
    }
  }

#pragma prefast(pop)


  // Lazily recomputes derived parameter values used by shader lighting calculations.
  _Use_decl_annotations_ void EffectStereoLights::SetConstants( int& dirtyFlags,
    EffectMatrices const& matrices,
      XMMATRIX& worldConstant,
      XMVECTOR worldInverseTransposeConstant[3],
      XMVECTOR& diffuseColorConstant,
      XMVECTOR& emissiveColorConstant,
      bool lightingEnabled )
  {
    if ( lightingEnabled )
    {
      // World inverse transpose matrix.
      if ( dirtyFlags & EffectDirtyFlags::WorldInverseTranspose )
      {
        worldConstant = XMMatrixTranspose( matrices.world );

        XMMATRIX worldInverse = XMMatrixInverse( nullptr, matrices.world );

        worldInverseTransposeConstant[0] = worldInverse.r[0];
        worldInverseTransposeConstant[1] = worldInverse.r[1];
        worldInverseTransposeConstant[2] = worldInverse.r[2];

        dirtyFlags &= ~EffectDirtyFlags::WorldInverseTranspose;
        dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
      }
    }

    // Material color parameters. The desired lighting model is:
    //
    //     ((ambientLightColor + sum(diffuse directional light)) * diffuseColor) + emissiveColor
    //
    // When lighting is disabled, ambient and directional lights are ignored, leaving:
    //
    //     diffuseColor + emissiveColor
    //
    // For the lighting disabled case, we can save one shader instruction by precomputing
    // diffuse+emissive on the CPU, after which the shader can use diffuseColor directly,
    // ignoring its emissive parameter.
    //
    // When lighting is enabled, we can merge the ambient and emissive settings. If we
    // set our emissive parameter to emissive+(ambient*diffuse), the shader no longer
    // needs to bother adding the ambient contribution, simplifying its computation to:
    //
    //     (sum(diffuse directional light) * diffuseColor) + emissiveColor
    //
    // For futher optimization goodness, we merge material alpha with the diffuse
    // color parameter, and premultiply all color values by this alpha.

    if ( dirtyFlags & EffectDirtyFlags::MaterialColor )
    {
      XMVECTOR diffuse = diffuseColor;
      XMVECTOR alphaVector = XMVectorReplicate( alpha );

      if ( lightingEnabled )
      {
        // Merge emissive and ambient light contributions.
        emissiveColorConstant = ( emissiveColor + ambientLightColor * diffuse ) * alphaVector;
      }
      else
      {
        // Merge diffuse and emissive light contributions.
        diffuse += emissiveColor;
      }

      // xyz = diffuse * alpha, w = alpha.
      diffuseColorConstant = XMVectorSelect( alphaVector, diffuse * alphaVector, g_XMSelect1110 );

      dirtyFlags &= ~EffectDirtyFlags::MaterialColor;
      dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }
  }

#pragma prefast(push)
#pragma prefast(disable:26015, "PREFAST doesn't understand that ValidateLightIndex bounds whichLight" )

  // Helper for turning one of the directional lights on or off.
  _Use_decl_annotations_ int EffectStereoLights::SetLightEnabled( int whichLight, bool value, XMVECTOR* lightDiffuseConstant, XMVECTOR* lightSpecularConstant )
  {
    ValidateLightIndex( whichLight );

    if ( lightEnabled[whichLight] == value )
    { return 0; }

    lightEnabled[whichLight] = value;

    if ( value )
    {
      // If this light is now on, store its color in the constant buffer.
      lightDiffuseConstant[whichLight] = lightDiffuseColor[whichLight];
      lightSpecularConstant[whichLight] = lightSpecularColor[whichLight];
    }
    else
    {
      // If the light is off, reset constant buffer colors to zero.
      lightDiffuseConstant[whichLight] = g_XMZero;
      lightSpecularConstant[whichLight] = g_XMZero;
    }

    return EffectDirtyFlags::ConstantBuffer;
  }

  // Helper for setting diffuse color of one of the directional lights.
  _Use_decl_annotations_
  int XM_CALLCONV EffectStereoLights::SetLightDiffuseColor( int whichLight, FXMVECTOR value, XMVECTOR* lightDiffuseConstant )
  {
    ValidateLightIndex( whichLight );

    // Locally store the new color.
    lightDiffuseColor[whichLight] = value;

    // If this light is currently on, also update the constant buffer.
    if ( lightEnabled[whichLight] )
    {
      lightDiffuseConstant[whichLight] = value;

      return EffectDirtyFlags::ConstantBuffer;
    }

    return 0;
  }

  // Helper for setting specular color of one of the directional lights.
  _Use_decl_annotations_
  int XM_CALLCONV EffectStereoLights::SetLightSpecularColor( int whichLight, FXMVECTOR value, XMVECTOR* lightSpecularConstant )
  {
    ValidateLightIndex( whichLight );

    // Locally store the new color.
    lightSpecularColor[whichLight] = value;

    // If this light is currently on, also update the constant buffer.
    if ( lightEnabled[whichLight] )
    {
      lightSpecularConstant[whichLight] = value;

      return EffectDirtyFlags::ConstantBuffer;
    }

    return 0;
  }

#pragma prefast(pop)

  // Parameter validation helper.
  void EffectStereoLights::ValidateLightIndex( int whichLight )
  {
    if ( whichLight < 0 || whichLight >= MaxDirectionalLights )
    {
      throw std::out_of_range( "whichLight parameter out of range" );
    }
  }

  // Activates the default lighting rig (key, fill, and back lights).
  void EffectStereoLights::EnableDefaultLighting( _In_ IEffectLights* effect )
  {
    static const XMVECTORF32 defaultDirections[MaxDirectionalLights] =
    {
      { -0.5265408f, -0.5735765f, -0.6275069f },
      { 0.7198464f,  0.3420201f,  0.6040227f },
      { 0.4545195f, -0.7660444f,  0.4545195f },
    };

    static const XMVECTORF32 defaultDiffuse[MaxDirectionalLights] =
    {
      { 1.0000000f, 0.9607844f, 0.8078432f },
      { 0.9647059f, 0.7607844f, 0.4078432f },
      { 0.3231373f, 0.3607844f, 0.3937255f },
    };

    static const XMVECTORF32 defaultSpecular[MaxDirectionalLights] =
    {
      { 1.0000000f, 0.9607844f, 0.8078432f },
      { 0.0000000f, 0.0000000f, 0.0000000f },
      { 0.3231373f, 0.3607844f, 0.3937255f },
    };

    static const XMVECTORF32 defaultAmbient = { 0.05333332f, 0.09882354f, 0.1819608f };

    effect->SetLightingEnabled( true );
    effect->SetAmbientLightColor( defaultAmbient );

    for ( int i = 0; i < MaxDirectionalLights; i++ )
    {
      effect->SetLightEnabled( i, true );
      effect->SetLightDirection( i, defaultDirections[i] );
      effect->SetLightDiffuseColor( i, defaultDiffuse[i] );
      effect->SetLightSpecularColor( i, defaultSpecular[i] );
    }
  }
}