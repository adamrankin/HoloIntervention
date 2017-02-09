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
#include "InstancedEffectCommon.h"
#include "InstancedEffects.h"

using Microsoft::WRL::ComPtr;

namespace DirectX
{
  // IEffectMatrices default method
  void XM_CALLCONV IStereoEffectMatrices::SetMatrices(FXMMATRIX world, FXMMATRIX view[2], FXMMATRIX projection[2])
  {
    SetWorld(world);
    SetView(view);
    SetProjection(projection);
  }

  // Constructor initializes default matrix values.
  StereoEffectMatrices::StereoEffectMatrices()
  {
    world = XMMatrixIdentity();
    view[0] = XMMatrixIdentity();
    view[1] = XMMatrixIdentity();
    projection[0] = XMMatrixIdentity();
    projection[1] = XMMatrixIdentity();
    worldView[0] = XMMatrixIdentity();
    worldView[1] = XMMatrixIdentity();
  }

  // Lazily recomputes the combined world+view+projection matrix.
  _Use_decl_annotations_ void StereoEffectMatrices::SetConstants(int& dirtyFlags,
      XMMATRIX& leftWorldViewProjConstant,
      XMMATRIX& rightWorldViewProjConstant)
  {
    if (dirtyFlags & EffectDirtyFlags::WorldViewProj)
    {
      worldView[0] = XMMatrixMultiply(world, view[0]);
      leftWorldViewProjConstant = XMMatrixMultiply(worldView[0], projection[0]);

      worldView[1] = XMMatrixMultiply(world, view[1]);
      rightWorldViewProjConstant = XMMatrixMultiply(worldView[1], projection[1]);

      dirtyFlags &= ~EffectDirtyFlags::WorldViewProj;
      dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }
  }

  // Constructor initializes default fog settings.
  StereoEffectFog::StereoEffectFog()
  {
    enabled = false;
    start = 0;
    end = 1;
  }

  // Lazily recomputes the derived vector used by shader fog calculations.
  _Use_decl_annotations_
  void XM_CALLCONV StereoEffectFog::SetConstants(int& dirtyFlags,
      FXMMATRIX leftWorldView,
      FXMMATRIX rightWorldView,
      XMVECTOR& leftFogVectorConstant,
      XMVECTOR& rightFogVectorConstant)
  {
    if (enabled)
    {
      if (dirtyFlags & (EffectDirtyFlags::FogVector | EffectDirtyFlags::FogEnable))
      {
        if (start == end)
        {
          // Degenerate case: force everything to 100% fogged if start and end are the same.
          static const XMVECTORF32 fullyFogged = { 0, 0, 0, 1 };

          leftFogVectorConstant = fullyFogged;
          rightFogVectorConstant = fullyFogged;
        }
        else
        {
          // We want to transform vertex positions into view space, take the resulting
          // Z value, then scale and offset according to the fog start/end distances.
          // Because we only care about the Z component, the shader can do all this
          // with a single dot product, using only the Z row of the world+view matrix.

          // 0, 0, 0, fogStart
          XMVECTOR wOffset = XMVectorSwizzle<1, 2, 3, 0>(XMLoadFloat(&start));

          // _13, _23, _33, _43
          XMVECTOR worldViewZ = XMVectorMergeXY(XMVectorMergeZW(leftWorldView.r[0], leftWorldView.r[2]),
                                                XMVectorMergeZW(leftWorldView.r[1], leftWorldView.r[3]));
          leftFogVectorConstant = (worldViewZ + wOffset) / (start - end);

          worldViewZ = XMVectorMergeXY(XMVectorMergeZW(rightWorldView.r[0], rightWorldView.r[2]),
                                       XMVectorMergeZW(rightWorldView.r[1], rightWorldView.r[3]));
          rightFogVectorConstant = (worldViewZ + wOffset) / (start - end);
        }

        dirtyFlags &= ~(EffectDirtyFlags::FogVector | EffectDirtyFlags::FogEnable);
        dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
      }
    }
    else
    {
      // When fog is disabled, make sure the fog vector is reset to zero.
      if (dirtyFlags & EffectDirtyFlags::FogEnable)
      {
        leftFogVectorConstant = g_XMZero;
        rightFogVectorConstant = g_XMZero;

        dirtyFlags &= ~EffectDirtyFlags::FogEnable;
        dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
      }
    }
  }

  // Constructor initializes default light settings.
  StereoEffectLights::StereoEffectLights()
  {
    emissiveColor = g_XMZero;
    ambientLightColor = g_XMZero;

    for (int i = 0; i < MaxDirectionalLights; i++)
    {
      lightEnabled[i] = (i == 0);
      lightDiffuseColor[i] = g_XMOne;
      lightSpecularColor[i] = g_XMZero;
    }
  }


#pragma prefast(push)
#pragma prefast(disable:22103, "PREFAST doesn't understand buffer is bounded by a static const value even with SAL" )

  // Initializes constant buffer fields to match the current lighting state.
  _Use_decl_annotations_ void StereoEffectLights::InitializeConstants(XMVECTOR& specularColorAndPowerConstant,
      XMVECTOR* lightDirectionConstant,
      XMVECTOR* lightDiffuseConstant,
      XMVECTOR* lightSpecularConstant)
  {
    static const XMVECTORF32 defaultSpecular = { 1, 1, 1, 16 };
    static const XMVECTORF32 defaultLightDirection = { 0, -1, 0, 0 };

    specularColorAndPowerConstant = defaultSpecular;

    for (int i = 0; i < MaxDirectionalLights; i++)
    {
      lightDirectionConstant[i] = defaultLightDirection;

      lightDiffuseConstant[i] = lightEnabled[i] ? lightDiffuseColor[i] : g_XMZero;
      lightSpecularConstant[i] = lightEnabled[i] ? lightSpecularColor[i] : g_XMZero;
    }
  }

#pragma prefast(pop)

  // Lazily recomputes derived parameter values used by shader lighting calculations.
  _Use_decl_annotations_ void StereoEffectLights::SetConstants(int& dirtyFlags,
      StereoEffectMatrices const& matrices,
      XMMATRIX& worldConstant,
      XMVECTOR worldInverseTransposeConstant[3],
      XMVECTOR& leftEyePositionConstant,
      XMVECTOR& rightEyePositionConstant,
      XMVECTOR& diffuseColorConstant,
      XMVECTOR& emissiveColorConstant,
      bool lightingEnabled)
  {
    if (lightingEnabled)
    {
      // World inverse transpose matrix.
      if (dirtyFlags & EffectDirtyFlags::WorldInverseTranspose)
      {
        worldConstant = matrices.world;

        auto worldInverseTranspose = XMMatrixInverse(nullptr, XMMatrixTranspose(matrices.world));

        worldInverseTransposeConstant[0] = worldInverseTranspose.r[0];
        worldInverseTransposeConstant[1] = worldInverseTranspose.r[1];
        worldInverseTransposeConstant[2] = worldInverseTranspose.r[2];

        dirtyFlags &= ~EffectDirtyFlags::WorldInverseTranspose;
        dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
      }

      // Eye position vector.
      if (dirtyFlags & EffectDirtyFlags::EyePosition)
      {
        XMMATRIX viewInverse = XMMatrixInverse(nullptr, matrices.view[0]);
        leftEyePositionConstant = viewInverse.r[3];

        viewInverse = XMMatrixInverse(nullptr, matrices.view[1]);
        rightEyePositionConstant = viewInverse.r[3];

        dirtyFlags &= ~EffectDirtyFlags::EyePosition;
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

    if (dirtyFlags & EffectDirtyFlags::MaterialColor)
    {
      XMVECTOR diffuse = diffuseColor;
      XMVECTOR alphaVector = XMVectorReplicate(alpha);

      if (lightingEnabled)
      {
        // Merge emissive and ambient light contributions.
        emissiveColorConstant = (emissiveColor + ambientLightColor * diffuse) * alphaVector;
      }
      else
      {
        // Merge diffuse and emissive light contributions.
        diffuse += emissiveColor;
      }

      // xyz = diffuse * alpha, w = alpha.
      diffuseColorConstant = XMVectorSelect(alphaVector, diffuse * alphaVector, g_XMSelect1110);

      dirtyFlags &= ~EffectDirtyFlags::MaterialColor;
      dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }
  }

#pragma prefast(push)
#pragma prefast(disable:26015, "PREFAST doesn't understand that ValidateLightIndex bounds whichLight" )

  // Helper for turning one of the directional lights on or off.
  _Use_decl_annotations_ int StereoEffectLights::SetLightEnabled(int whichLight, bool value, XMVECTOR* lightDiffuseConstant, XMVECTOR* lightSpecularConstant)
  {
    ValidateLightIndex(whichLight);

    if (lightEnabled[whichLight] == value)
    {
      return 0;
    }

    lightEnabled[whichLight] = value;

    if (value)
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
  int XM_CALLCONV StereoEffectLights::SetLightDiffuseColor(int whichLight, FXMVECTOR value, XMVECTOR* lightDiffuseConstant)
  {
    ValidateLightIndex(whichLight);

    // Locally store the new color.
    lightDiffuseColor[whichLight] = value;

    // If this light is currently on, also update the constant buffer.
    if (lightEnabled[whichLight])
    {
      lightDiffuseConstant[whichLight] = value;

      return EffectDirtyFlags::ConstantBuffer;
    }

    return 0;
  }

  // Helper for setting specular color of one of the directional lights.
  _Use_decl_annotations_
  int XM_CALLCONV StereoEffectLights::SetLightSpecularColor(int whichLight, FXMVECTOR value, XMVECTOR* lightSpecularConstant)
  {
    ValidateLightIndex(whichLight);

    // Locally store the new color.
    lightSpecularColor[whichLight] = value;

    // If this light is currently on, also update the constant buffer.
    if (lightEnabled[whichLight])
    {
      lightSpecularConstant[whichLight] = value;

      return EffectDirtyFlags::ConstantBuffer;
    }

    return 0;
  }

#pragma prefast(pop)

  // Parameter validation helper.
  void StereoEffectLights::ValidateLightIndex(int whichLight)
  {
    if (whichLight < 0 || whichLight >= MaxDirectionalLights)
    {
      throw std::out_of_range("whichLight parameter out of range");
    }
  }

  // Activates the default lighting rig (key, fill, and back lights).
  void StereoEffectLights::EnableDefaultLighting(_In_ IEffectLights* effect)
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

    effect->SetLightingEnabled(true);
    effect->SetAmbientLightColor(defaultAmbient);

    for (int i = 0; i < MaxDirectionalLights; i++)
    {
      effect->SetLightEnabled(i, true);
      effect->SetLightDirection(i, defaultDirections[i]);
      effect->SetLightDiffuseColor(i, defaultDiffuse[i]);
      effect->SetLightSpecularColor(i, defaultSpecular[i]);
    }
  }
}