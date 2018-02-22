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

// Local includes
#include "pch.h"
#include "InstancedEffectCommon.h"

// DirectXTK includes
#include <EffectCommon.h>

using namespace DirectX;

// Constant buffer layout. Must match the shader!
struct InstancedBasicEffectConstants
{
  XMVECTOR diffuseColor;
  XMVECTOR emissiveColor;
  XMVECTOR specularColorAndPower;

  XMVECTOR lightDirection[IEffectLights::MaxDirectionalLights];
  XMVECTOR lightDiffuseColor[IEffectLights::MaxDirectionalLights];
  XMVECTOR lightSpecularColor[IEffectLights::MaxDirectionalLights];

  XMVECTOR eyePosition[2];

  XMVECTOR fogColor;
  XMVECTOR fogVector[2];

  XMMATRIX world;
  XMVECTOR worldInverseTranspose[3];
  XMMATRIX worldViewProj[2];
};

static_assert((sizeof(InstancedBasicEffectConstants) % (sizeof(float) * 4)) == 0, "InstancedBasicEffectConstants size must be 16-byte aligned (16 bytes is the length of four floats).");

// Traits type describes our characteristics to the EffectBase template.
struct BasicEffectTraits
{
  typedef InstancedBasicEffectConstants ConstantBufferType;

  static const int VertexShaderCount = 20;
  static const int GeometryShaderCount = 8;
  static const int PixelShaderCount = 10;
  static const int ShaderPermutationCount = 32;
};

//-------------------------------------------------------------------
// Vertex shaders
#include <InstancedBasicEffect_VSBasic_VPRT.inc>
#include <InstancedBasicEffect_VSBasicNoFog_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVcNoFog_VPRT.inc>
#include <InstancedBasicEffect_VSBasicTx_VPRT.inc>
#include <InstancedBasicEffect_VSBasicTxNoFog_VPRT.inc>
#include <InstancedBasicEffect_VSBasicTxVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicTxVcNoFog_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVertexLighting_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingTx_VPRT.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingTxVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicOneLight_VPRT.inc>
#include <InstancedBasicEffect_VSBasicOneLightVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicOneLightTx_VPRT.inc>
#include <InstancedBasicEffect_VSBasicOneLightTxVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicPixelLighting_VPRT.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingVc_VPRT.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingTx_VPRT.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingTxVc_VPRT.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::VPRTVertexShaderBytecode[] =
{
  { InstancedBasicEffect_VSBasicVPRT,                    sizeof(InstancedBasicEffect_VSBasicVPRT) },
  { InstancedBasicEffect_VSBasicNoFogVPRT,               sizeof(InstancedBasicEffect_VSBasicNoFogVPRT) },
  { InstancedBasicEffect_VSBasicVcVPRT,                  sizeof(InstancedBasicEffect_VSBasicVcVPRT) },
  { InstancedBasicEffect_VSBasicVcNoFogVPRT,             sizeof(InstancedBasicEffect_VSBasicVcNoFogVPRT) },
  { InstancedBasicEffect_VSBasicTxVPRT,                  sizeof(InstancedBasicEffect_VSBasicTxVPRT) },
  { InstancedBasicEffect_VSBasicTxNoFogVPRT,             sizeof(InstancedBasicEffect_VSBasicTxNoFogVPRT) },
  { InstancedBasicEffect_VSBasicTxVcVPRT,                sizeof(InstancedBasicEffect_VSBasicTxVcVPRT) },
  { InstancedBasicEffect_VSBasicTxVcNoFogVPRT,           sizeof(InstancedBasicEffect_VSBasicTxVcNoFogVPRT) },

  { InstancedBasicEffect_VSBasicVertexLightingVPRT,      sizeof(InstancedBasicEffect_VSBasicVertexLightingVPRT) },
  { InstancedBasicEffect_VSBasicVertexLightingVcVPRT,    sizeof(InstancedBasicEffect_VSBasicVertexLightingVcVPRT) },
  { InstancedBasicEffect_VSBasicVertexLightingTxVPRT,    sizeof(InstancedBasicEffect_VSBasicVertexLightingTxVPRT) },
  { InstancedBasicEffect_VSBasicVertexLightingTxVcVPRT,  sizeof(InstancedBasicEffect_VSBasicVertexLightingTxVcVPRT) },

  { InstancedBasicEffect_VSBasicOneLightVPRT,            sizeof(InstancedBasicEffect_VSBasicOneLightVPRT) },
  { InstancedBasicEffect_VSBasicOneLightVcVPRT,          sizeof(InstancedBasicEffect_VSBasicOneLightVcVPRT) },
  { InstancedBasicEffect_VSBasicOneLightTxVPRT,          sizeof(InstancedBasicEffect_VSBasicOneLightTxVPRT) },
  { InstancedBasicEffect_VSBasicOneLightTxVcVPRT,        sizeof(InstancedBasicEffect_VSBasicOneLightTxVcVPRT) },

  { InstancedBasicEffect_VSBasicPixelLightingVPRT,       sizeof(InstancedBasicEffect_VSBasicPixelLightingVPRT) },
  { InstancedBasicEffect_VSBasicPixelLightingVcVPRT,     sizeof(InstancedBasicEffect_VSBasicPixelLightingVcVPRT) },
  { InstancedBasicEffect_VSBasicPixelLightingTxVPRT,     sizeof(InstancedBasicEffect_VSBasicPixelLightingTxVPRT) },
  { InstancedBasicEffect_VSBasicPixelLightingTxVcVPRT,   sizeof(InstancedBasicEffect_VSBasicPixelLightingTxVcVPRT) },
};

#include <InstancedBasicEffect_VSBasic.inc>
#include <InstancedBasicEffect_VSBasicNoFog.inc>
#include <InstancedBasicEffect_VSBasicVc.inc>
#include <InstancedBasicEffect_VSBasicVcNoFog.inc>
#include <InstancedBasicEffect_VSBasicTx.inc>
#include <InstancedBasicEffect_VSBasicTxNoFog.inc>
#include <InstancedBasicEffect_VSBasicTxVc.inc>
#include <InstancedBasicEffect_VSBasicTxVcNoFog.inc>
#include <InstancedBasicEffect_VSBasicVertexLighting.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingVc.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingTx.inc>
#include <InstancedBasicEffect_VSBasicVertexLightingTxVc.inc>
#include <InstancedBasicEffect_VSBasicOneLight.inc>
#include <InstancedBasicEffect_VSBasicOneLightVc.inc>
#include <InstancedBasicEffect_VSBasicOneLightTx.inc>
#include <InstancedBasicEffect_VSBasicOneLightTxVc.inc>
#include <InstancedBasicEffect_VSBasicPixelLighting.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingVc.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingTx.inc>
#include <InstancedBasicEffect_VSBasicPixelLightingTxVc.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::VertexShaderBytecode[] =
{
  { InstancedBasicEffect_VSBasic,                    sizeof(InstancedBasicEffect_VSBasic) },
  { InstancedBasicEffect_VSBasicNoFog,               sizeof(InstancedBasicEffect_VSBasicNoFog) },
  { InstancedBasicEffect_VSBasicVc,                  sizeof(InstancedBasicEffect_VSBasicVc) },
  { InstancedBasicEffect_VSBasicVcNoFog,             sizeof(InstancedBasicEffect_VSBasicVcNoFog) },
  { InstancedBasicEffect_VSBasicTx,                  sizeof(InstancedBasicEffect_VSBasicTx) },
  { InstancedBasicEffect_VSBasicTxNoFog,             sizeof(InstancedBasicEffect_VSBasicTxNoFog) },
  { InstancedBasicEffect_VSBasicTxVc,                sizeof(InstancedBasicEffect_VSBasicTxVc) },
  { InstancedBasicEffect_VSBasicTxVcNoFog,           sizeof(InstancedBasicEffect_VSBasicTxVcNoFog) },

  { InstancedBasicEffect_VSBasicVertexLighting,      sizeof(InstancedBasicEffect_VSBasicVertexLighting) },
  { InstancedBasicEffect_VSBasicVertexLightingVc,    sizeof(InstancedBasicEffect_VSBasicVertexLightingVc) },
  { InstancedBasicEffect_VSBasicVertexLightingTx,    sizeof(InstancedBasicEffect_VSBasicVertexLightingTx) },
  { InstancedBasicEffect_VSBasicVertexLightingTxVc,  sizeof(InstancedBasicEffect_VSBasicVertexLightingTxVc) },

  { InstancedBasicEffect_VSBasicOneLight,            sizeof(InstancedBasicEffect_VSBasicOneLight) },
  { InstancedBasicEffect_VSBasicOneLightVc,          sizeof(InstancedBasicEffect_VSBasicOneLightVc) },
  { InstancedBasicEffect_VSBasicOneLightTx,          sizeof(InstancedBasicEffect_VSBasicOneLightTx) },
  { InstancedBasicEffect_VSBasicOneLightTxVc,        sizeof(InstancedBasicEffect_VSBasicOneLightTxVc) },

  { InstancedBasicEffect_VSBasicPixelLighting,       sizeof(InstancedBasicEffect_VSBasicPixelLighting) },
  { InstancedBasicEffect_VSBasicPixelLightingVc,     sizeof(InstancedBasicEffect_VSBasicPixelLightingVc) },
  { InstancedBasicEffect_VSBasicPixelLightingTx,     sizeof(InstancedBasicEffect_VSBasicPixelLightingTx) },
  { InstancedBasicEffect_VSBasicPixelLightingTxVc,   sizeof(InstancedBasicEffect_VSBasicPixelLightingTxVc) },
};

const int InstancedEffectBase<BasicEffectTraits>::VertexShaderIndices[] =
{
  0,      // basic
  1,      // no fog
  2,      // vertex color
  3,      // vertex color, no fog
  4,      // texture
  5,      // texture, no fog
  6,      // texture + vertex color
  7,      // texture + vertex color, no fog

  8,      // vertex lighting
  8,      // vertex lighting, no fog
  9,      // vertex lighting + vertex color
  9,      // vertex lighting + vertex color, no fog
  10,     // vertex lighting + texture
  10,     // vertex lighting + texture, no fog
  11,     // vertex lighting + texture + vertex color
  11,     // vertex lighting + texture + vertex color, no fog

  12,     // one light
  12,     // one light, no fog
  13,     // one light + vertex color
  13,     // one light + vertex color, no fog
  14,     // one light + texture
  14,     // one light + texture, no fog
  15,     // one light + texture + vertex color
  15,     // one light + texture + vertex color, no fog

  16,     // pixel lighting
  16,     // pixel lighting, no fog
  17,     // pixel lighting + vertex color
  17,     // pixel lighting + vertex color, no fog
  18,     // pixel lighting + texture
  18,     // pixel lighting + texture, no fog
  19,     // pixel lighting + texture + vertex color
  19,     // pixel lighting + texture + vertex color, no fog
};

//-------------------------------------------------------------------
// Geometry shaders
#include <PCCIGeometryShader.inc>
#include <PCCTIGeometryShader.inc>
#include <PCIGeometryShader.inc>
#include <PCTIGeometryShader.inc>
#include <PTIGeometryShader.inc>
#include <PPNCIGeometryShader.inc>

// Pixel lighting passthrough shaders
#include <PCT0T4IGeometryShader.inc>
#include <PCT0T1T2IGeometryShader.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::GeometryShaderBytecode[] =
{
  { PCCIGeometryShader,               sizeof(PCCIGeometryShader) },
  { PCCTIGeometryShader,              sizeof(PCCTIGeometryShader) },
  { PCIGeometryShader,                sizeof(PCIGeometryShader) },
  { PCTIGeometryShader,               sizeof(PCTIGeometryShader) },
  { PTIGeometryShader,                sizeof(PTIGeometryShader) },
  { PPNCIGeometryShader,              sizeof(PPNCIGeometryShader) },

  { PCT0T4IGeometryShader,            sizeof(PCT0T4IGeometryShader) },
  { PCT0T1T2IGeometryShader,          sizeof(PCT0T1T2IGeometryShader) },
};

const int InstancedEffectBase<BasicEffectTraits>::GeometryShaderIndices[] =
{
  0,      // basic
  2,      // no fog
  0,      // vertex color
  2,      // vertex color, no fog
  1,      // texture
  3,      // texture, no fog
  1,      // texture + vertex color
  3,      // texture + vertex color, no fog

  0,      // vertex lighting
  0,      // vertex lighting, no fog
  0,      // vertex lighting + vertex color
  0,      // vertex lighting + vertex color, no fog
  1,      // vertex lighting + texture
  1,      // vertex lighting + texture, no fog
  1,      // vertex lighting + texture + vertex color
  1,      // vertex lighting + texture + vertex color, no fog

  0,      // one light
  0,      // one light, no fog
  0,      // one light + vertex color
  0,      // one light + vertex color, no fog
  1,      // one light + texture
  1,      // one light + texture, no fog
  1,      // one light + texture + vertex color
  1,      // one light + texture + vertex color, no fog

  5,      // pixel lighting
  5,      // pixel lighting, no fog
  5,      // pixel lighting + vertex color
  5,      // pixel lighting + vertex color, no fog
  6,      // pixel lighting + texture
  6,      // pixel lighting + texture, no fog
  6,      // pixel lighting + texture + vertex color
  6,      // pixel lighting + texture + vertex color, no fog
};

//-------------------------------------------------------------------
// Pixel shaders
#include <InstancedBasicEffect_PSBasic.inc>
#include <InstancedBasicEffect_PSBasicNoFog.inc>
#include <InstancedBasicEffect_PSBasicTx.inc>
#include <InstancedBasicEffect_PSBasicTxNoFog.inc>
#include <InstancedBasicEffect_PSBasicVertexLighting.inc>
#include <InstancedBasicEffect_PSBasicVertexLightingNoFog.inc>
#include <InstancedBasicEffect_PSBasicVertexLightingTx.inc>
#include <InstancedBasicEffect_PSBasicVertexLightingTxNoFog.inc>
#include <InstancedBasicEffect_PSBasicPixelLighting.inc>
#include <InstancedBasicEffect_PSBasicPixelLightingTx.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::PixelShaderBytecode[] =
{
  { InstancedBasicEffect_PSBasic,                      sizeof(InstancedBasicEffect_PSBasic) },
  { InstancedBasicEffect_PSBasicNoFog,                 sizeof(InstancedBasicEffect_PSBasicNoFog) },
  { InstancedBasicEffect_PSBasicTx,                    sizeof(InstancedBasicEffect_PSBasicTx) },
  { InstancedBasicEffect_PSBasicTxNoFog,               sizeof(InstancedBasicEffect_PSBasicTxNoFog) },

  { InstancedBasicEffect_PSBasicVertexLighting,        sizeof(InstancedBasicEffect_PSBasicVertexLighting) },
  { InstancedBasicEffect_PSBasicVertexLightingNoFog,   sizeof(InstancedBasicEffect_PSBasicVertexLightingNoFog) },
  { InstancedBasicEffect_PSBasicVertexLightingTx,      sizeof(InstancedBasicEffect_PSBasicVertexLightingTx) },
  { InstancedBasicEffect_PSBasicVertexLightingTxNoFog, sizeof(InstancedBasicEffect_PSBasicVertexLightingTxNoFog) },

  { InstancedBasicEffect_PSBasicPixelLighting,         sizeof(InstancedBasicEffect_PSBasicPixelLighting) },
  { InstancedBasicEffect_PSBasicPixelLightingTx,       sizeof(InstancedBasicEffect_PSBasicPixelLightingTx) },
};


const int InstancedEffectBase<BasicEffectTraits>::PixelShaderIndices[] =
{
  0,      // basic
  1,      // no fog
  0,      // vertex color
  1,      // vertex color, no fog
  2,      // texture
  3,      // texture, no fog
  2,      // texture + vertex color
  3,      // texture + vertex color, no fog

  4,      // vertex lighting
  5,      // vertex lighting, no fog
  4,      // vertex lighting + vertex color
  5,      // vertex lighting + vertex color, no fog
  6,      // vertex lighting + texture
  7,      // vertex lighting + texture, no fog
  6,      // vertex lighting + texture + vertex color
  7,      // vertex lighting + texture + vertex color, no fog

  4,      // one light
  5,      // one light, no fog
  4,      // one light + vertex color
  5,      // one light + vertex color, no fog
  6,      // one light + texture
  7,      // one light + texture, no fog
  6,      // one light + texture + vertex color
  7,      // one light + texture + vertex color, no fog

  8,      // pixel lighting
  8,      // pixel lighting, no fog
  8,      // pixel lighting + vertex color
  8,      // pixel lighting + vertex color, no fog
  9,      // pixel lighting + texture
  9,      // pixel lighting + texture, no fog
  9,      // pixel lighting + texture + vertex color
  9,      // pixel lighting + texture + vertex color, no fog
};

// Global pool of per-device BasicEffect resources.
SharedResourcePool<ID3D11Device*, InstancedEffectBase<BasicEffectTraits>::DeviceResources> InstancedEffectBase<BasicEffectTraits>::deviceResourcesPool;

namespace DirectX
{
  // Internal BasicInstancedLightingEffect implementation class.
  class InstancedBasicEffect::Impl : public InstancedEffectBase<BasicEffectTraits>
  {
  public:
    Impl(_In_ ID3D11Device* device);

    bool lightingEnabled;
    bool preferPerPixelLighting;
    bool vertexColorEnabled;
    bool textureEnabled;

    StereoEffectLights lights;

    int GetCurrentShaderPermutation() const;

    void Apply(_In_ ID3D11DeviceContext* deviceContext);
  };

  // Constructor.
  InstancedBasicEffect::Impl::Impl(_In_ ID3D11Device* device)
    : InstancedEffectBase(device),
      lightingEnabled(false),
      preferPerPixelLighting(false),
      vertexColorEnabled(false),
      textureEnabled(false)
  {
    lights.InitializeConstants(constants.specularColorAndPower, constants.lightDirection, constants.lightDiffuseColor, constants.lightSpecularColor);
  }

  int InstancedBasicEffect::Impl::GetCurrentShaderPermutation() const
  {
    int permutation = 0;

    // Use optimized shaders if fog is disabled.
    if (!fog.enabled)
    {
      permutation += 1;
    }

    // Support vertex coloring?
    if (vertexColorEnabled)
    {
      permutation += 2;
    }

    // Support texturing?
    if (textureEnabled)
    {
      permutation += 4;
    }

    if (lightingEnabled)
    {
      if (preferPerPixelLighting)
      {
        // Do lighting in the pixel shader.
        permutation += 24;
      }
      else if (!lights.lightEnabled[1] && !lights.lightEnabled[2])
      {
        // Use the only-bother-with-the-first-light shader optimization.
        permutation += 16;
      }
      else
      {
        // Compute all three lights in the vertex shader.
        permutation += 8;
      }
    }

    return permutation;
  }


  // Sets our state onto the D3D device.
  void InstancedBasicEffect::Impl::Apply(_In_ ID3D11DeviceContext* deviceContext)
  {
    matrices.SetConstants(dirtyFlags, constants.worldViewProj[0], constants.worldViewProj[1]);
    fog.SetConstants(dirtyFlags, matrices.worldView[0], matrices.worldView[1], constants.fogVector[0], constants.fogVector[1]);
    lights.SetConstants(dirtyFlags,
                        matrices,
                        constants.world,
                        constants.worldInverseTranspose,
                        constants.eyePosition[0],
                        constants.eyePosition[1],
                        constants.diffuseColor,
                        constants.emissiveColor,
                        lightingEnabled);

    // Set the texture.
    if (textureEnabled)
    {
      ID3D11ShaderResourceView* textures[1] = { texture.Get() };

      deviceContext->PSSetShaderResources(0, 1, textures);
    }

    // Set shaders and constant buffers.
    ApplyShaders(deviceContext, GetCurrentShaderPermutation());
  }


  // Public constructor.
  InstancedBasicEffect::InstancedBasicEffect(_In_ ID3D11Device* device)
    : pImpl(new Impl(device))
  {
  }


  // Move constructor.
  InstancedBasicEffect::InstancedBasicEffect(InstancedBasicEffect&& moveFrom)
    : pImpl(std::move(moveFrom.pImpl))
  {
  }


  // Move assignment.
  InstancedBasicEffect& InstancedBasicEffect::operator= (InstancedBasicEffect&& moveFrom)
  {
    pImpl = std::move(moveFrom.pImpl);
    return *this;
  }


  // Public destructor.
  InstancedBasicEffect::~InstancedBasicEffect()
  {
  }


  // IEffect methods.
  void InstancedBasicEffect::Apply(_In_ ID3D11DeviceContext* deviceContext)
  {
    pImpl->Apply(deviceContext);
  }


  void InstancedBasicEffect::GetVertexShaderBytecode(_Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength)
  {
    pImpl->GetVertexShaderBytecode(pImpl->GetCurrentShaderPermutation(), pShaderByteCode, pByteCodeLength);
  }


  // Camera settings.
  void XM_CALLCONV InstancedBasicEffect::SetWorld(FXMMATRIX value)
  {
    pImpl->matrices.world = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::WorldViewProj | EffectDirtyFlags::WorldInverseTranspose | EffectDirtyFlags::FogVector;
  }


  void XM_CALLCONV InstancedBasicEffect::SetView(FXMMATRIX leftView, CXMMATRIX rightView)
  {
    pImpl->matrices.view[0] = leftView;
    pImpl->matrices.view[1] = rightView;
  }


  void XM_CALLCONV InstancedBasicEffect::SetProjection(FXMMATRIX leftProjection, CXMMATRIX rightProjection)
  {
    pImpl->matrices.projection[0] = leftProjection;
    pImpl->matrices.projection[1] = rightProjection;
  }


  void XM_CALLCONV InstancedBasicEffect::SetMatrices(FXMMATRIX world, CXMMATRIX leftView, CXMMATRIX rightView, CXMMATRIX leftProjection, CXMMATRIX rightProjection)
  {
    pImpl->matrices.world = world;

    pImpl->matrices.view[0] = leftView;
    pImpl->matrices.view[1] = rightView;

    pImpl->matrices.projection[0] = leftProjection;
    pImpl->matrices.projection[1] = rightProjection;

    pImpl->dirtyFlags |= EffectDirtyFlags::WorldViewProj | EffectDirtyFlags::WorldInverseTranspose | EffectDirtyFlags::EyePosition | EffectDirtyFlags::FogVector;
  }


  // Material settings.
  void XM_CALLCONV InstancedBasicEffect::SetDiffuseColor(FXMVECTOR value)
  {
    pImpl->lights.diffuseColor = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  FXMVECTOR XM_CALLCONV InstancedBasicEffect::GetDiffuseColor() const
  {
    return pImpl->lights.diffuseColor;
  }


  void XM_CALLCONV InstancedBasicEffect::SetEmissiveColor(FXMVECTOR value)
  {
    pImpl->lights.emissiveColor = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  FXMVECTOR XM_CALLCONV InstancedBasicEffect::GetEmissiveColor() const
  {
    return pImpl->lights.emissiveColor;
  }


  void XM_CALLCONV InstancedBasicEffect::SetSpecularColor(FXMVECTOR value)
  {
    // Set xyz to new value, but preserve existing w (specular power).
    pImpl->constants.specularColorAndPower = XMVectorSelect(pImpl->constants.specularColorAndPower, value, g_XMSelect1110);

    pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
  }


  void InstancedBasicEffect::SetSpecularPower(float value)
  {
    // Set w to new value, but preserve existing xyz (specular color).
    pImpl->constants.specularColorAndPower = XMVectorSetW(pImpl->constants.specularColorAndPower, value);

    pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
  }


  void InstancedBasicEffect::DisableSpecular()
  {
    // Set specular color to black, power to 1
    // Note: Don't use a power of 0 or the shader will generate strange highlights on non-specular materials

    pImpl->constants.specularColorAndPower = g_XMIdentityR3;

    pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
  }


  void InstancedBasicEffect::SetAlpha(float value)
  {
    pImpl->lights.alpha = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  float InstancedBasicEffect::GetAlpha() const
  {
    return pImpl->lights.alpha;
  }


  void XM_CALLCONV InstancedBasicEffect::SetColorAndAlpha(FXMVECTOR value)
  {
    pImpl->lights.diffuseColor = value;
    pImpl->lights.alpha = XMVectorGetW(value);

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  // Light settings.
  void InstancedBasicEffect::SetLightingEnabled(bool value)
  {
    pImpl->lightingEnabled = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  void InstancedBasicEffect::SetPerPixelLighting(bool value)
  {
    pImpl->preferPerPixelLighting = value;
  }


  void XM_CALLCONV InstancedBasicEffect::SetAmbientLightColor(FXMVECTOR value)
  {
    pImpl->lights.ambientLightColor = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
  }


  void InstancedBasicEffect::SetLightEnabled(int whichLight, bool value)
  {
    pImpl->dirtyFlags |= pImpl->lights.SetLightEnabled(whichLight, value, pImpl->constants.lightDiffuseColor, pImpl->constants.lightSpecularColor);
  }


  void XM_CALLCONV InstancedBasicEffect::SetLightDirection(int whichLight, FXMVECTOR value)
  {
    EffectLights::ValidateLightIndex(whichLight);

    pImpl->constants.lightDirection[whichLight] = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
  }


  void XM_CALLCONV InstancedBasicEffect::SetLightDiffuseColor(int whichLight, FXMVECTOR value)
  {
    pImpl->dirtyFlags |= pImpl->lights.SetLightDiffuseColor(whichLight, value, pImpl->constants.lightDiffuseColor);
  }


  void XM_CALLCONV InstancedBasicEffect::SetLightSpecularColor(int whichLight, FXMVECTOR value)
  {
    pImpl->dirtyFlags |= pImpl->lights.SetLightSpecularColor(whichLight, value, pImpl->constants.lightSpecularColor);
  }


  void InstancedBasicEffect::EnableDefaultLighting()
  {
    EffectLights::EnableDefaultLighting(this);
  }

  // Fog settings.
  void InstancedBasicEffect::SetFogEnabled(bool value)
  {
    pImpl->fog.enabled = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::FogEnable;
  }


  void InstancedBasicEffect::SetFogStart(float value)
  {
    pImpl->fog.start = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::FogVector;
  }


  void InstancedBasicEffect::SetFogEnd(float value)
  {
    pImpl->fog.end = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::FogVector;
  }


  void XM_CALLCONV InstancedBasicEffect::SetFogColor(FXMVECTOR value)
  {
    pImpl->constants.fogColor = value;

    pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
  }


  // Vertex color setting.
  void InstancedBasicEffect::SetVertexColorEnabled(bool value)
  {
    pImpl->vertexColorEnabled = value;
  }


  // Texture settings.
  void InstancedBasicEffect::SetTextureEnabled(bool value)
  {
    pImpl->textureEnabled = value;
  }


  void InstancedBasicEffect::SetTexture(_In_opt_ ID3D11ShaderResourceView* value)
  {
    pImpl->texture = value;
  }
}