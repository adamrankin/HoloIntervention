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

// Local includes
#include "pch.h"
#include "InstancedBasicEffect.h"
#include "InstancedEffectBase.h"

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
};

// Traits type describes our characteristics to the EffectBase template.
struct BasicEffectTraits
{
  typedef InstancedBasicEffectConstants ConstantBufferType;

  static const int VertexShaderCount = 1;
  static const int PixelShaderCount = 1;
  static const int ShaderPermutationCount = 1;
};

// Load pre-compiled vertex shaders
#include <BasicLightingVertexShader.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::VertexShaderBytecode[] =
{
  { BasicLightingVertexShader,              sizeof( BasicLightingVertexShader ) },
  /*
  { BasicEffect_VSBasicNoFog,               sizeof(BasicEffect_VSBasicNoFog) },
  { BasicEffect_VSBasicVc,                  sizeof(BasicEffect_VSBasicVc) },
  { BasicEffect_VSBasicVcNoFog,             sizeof(BasicEffect_VSBasicVcNoFog) },
  { BasicEffect_VSBasicTx,                  sizeof(BasicEffect_VSBasicTx) },
  { BasicEffect_VSBasicTxNoFog,             sizeof(BasicEffect_VSBasicTxNoFog) },
  { BasicEffect_VSBasicTxVc,                sizeof(BasicEffect_VSBasicTxVc) },
  { BasicEffect_VSBasicTxVcNoFog,           sizeof(BasicEffect_VSBasicTxVcNoFog) },

  { BasicEffect_VSBasicVertexLighting,      sizeof(BasicEffect_VSBasicVertexLighting) },
  { BasicEffect_VSBasicVertexLightingVc,    sizeof(BasicEffect_VSBasicVertexLightingVc) },
  { BasicEffect_VSBasicVertexLightingTx,    sizeof(BasicEffect_VSBasicVertexLightingTx) },
  { BasicEffect_VSBasicVertexLightingTxVc,  sizeof(BasicEffect_VSBasicVertexLightingTxVc) },

  { BasicEffect_VSBasicOneLight,            sizeof(BasicEffect_VSBasicOneLight) },
  { BasicEffect_VSBasicOneLightVc,          sizeof(BasicEffect_VSBasicOneLightVc) },
  { BasicEffect_VSBasicOneLightTx,          sizeof(BasicEffect_VSBasicOneLightTx) },
  { BasicEffect_VSBasicOneLightTxVc,        sizeof(BasicEffect_VSBasicOneLightTxVc) },

  { BasicEffect_VSBasicPixelLighting,       sizeof(BasicEffect_VSBasicPixelLighting) },
  { BasicEffect_VSBasicPixelLightingVc,     sizeof(BasicEffect_VSBasicPixelLightingVc) },
  { BasicEffect_VSBasicPixelLightingTx,     sizeof(BasicEffect_VSBasicPixelLightingTx) },
  { BasicEffect_VSBasicPixelLightingTxVc,   sizeof(BasicEffect_VSBasicPixelLightingTxVc) },
  */
};

const int InstancedEffectBase<BasicEffectTraits>::VertexShaderIndices[] =
{
  0,      // basic, one light
};

// Load pre-compiled vertex shaders
#include <BasicLightingPixelShader.inc>

const ShaderBytecode InstancedEffectBase<BasicEffectTraits>::PixelShaderBytecode[] =
{
  { BasicLightingPixelShader,                 sizeof( BasicLightingPixelShader ) },
  /*
  { BasicEffect_PSBasicNoFog,                 sizeof(BasicEffect_PSBasicNoFog) },
  { BasicEffect_PSBasicTx,                    sizeof(BasicEffect_PSBasicTx) },
  { BasicEffect_PSBasicTxNoFog,               sizeof(BasicEffect_PSBasicTxNoFog) },

  { BasicEffect_PSBasicVertexLighting,        sizeof(BasicEffect_PSBasicVertexLighting) },
  { BasicEffect_PSBasicVertexLightingNoFog,   sizeof(BasicEffect_PSBasicVertexLightingNoFog) },
  { BasicEffect_PSBasicVertexLightingTx,      sizeof(BasicEffect_PSBasicVertexLightingTx) },
  { BasicEffect_PSBasicVertexLightingTxNoFog, sizeof(BasicEffect_PSBasicVertexLightingTxNoFog) },

  { BasicEffect_PSBasicPixelLighting,         sizeof(BasicEffect_PSBasicPixelLighting) },
  { BasicEffect_PSBasicPixelLightingTx,       sizeof(BasicEffect_PSBasicPixelLightingTx) },
  */
};


const int InstancedEffectBase<BasicEffectTraits>::PixelShaderIndices[] =
{
  0,      // basic, one light
};

// Global pool of per-device BasicEffect resources.
SharedResourcePool<ID3D11Device*, InstancedEffectBase<BasicEffectTraits>::DeviceResources> InstancedEffectBase<BasicEffectTraits>::deviceResourcesPool;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    // Internal BasicInstancedLightingEffect implementation class.
    class InstancedBasicEffect::Impl : public InstancedEffectBase<BasicEffectTraits>
    {
    public:
      Impl( _In_ ID3D11Device* device );

      bool lightingEnabled;
      bool preferPerPixelLighting;
      bool vertexColorEnabled;
      bool textureEnabled;

      EffectStereoLights lights;

      int GetCurrentShaderPermutation() const;

      void Apply( _In_ ID3D11DeviceContext* deviceContext );
    };

    // Constructor.
    InstancedBasicEffect::Impl::Impl( _In_ ID3D11Device* device )
      : InstancedEffectBase( device ),
        lightingEnabled( false ),
        preferPerPixelLighting( false ),
        vertexColorEnabled( false ),
        textureEnabled( false )
    {
      lights.InitializeConstants( constants.specularColorAndPower, constants.lightDirection, constants.lightDiffuseColor, constants.lightSpecularColor );
    }

    int InstancedBasicEffect::Impl::GetCurrentShaderPermutation() const
    {
      // For now, only one effect is supported
      return 0;

      /*
      int permutation = 0;

      // Use optimized shaders if fog is disabled.
      if ( !fog.enabled )
      {
        permutation += 1;
      }

      // Support vertex coloring?
      if ( vertexColorEnabled )
      {
        permutation += 2;
      }

      // Support texturing?
      if ( textureEnabled )
      {
        permutation += 4;
      }

      if ( lightingEnabled )
      {
        if ( preferPerPixelLighting )
        {
          // Do lighting in the pixel shader.
          permutation += 24;
        }
        else if ( !lights.lightEnabled[1] && !lights.lightEnabled[2] )
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
      */
    }


    // Sets our state onto the D3D device.
    void InstancedBasicEffect::Impl::Apply( _In_ ID3D11DeviceContext* deviceContext )
    {
      lights.SetConstants( dirtyFlags, matrices, constants.world, constants.worldInverseTranspose, constants.diffuseColor, constants.emissiveColor, lightingEnabled );

      // Set the texture.
      if ( textureEnabled )
      {
        ID3D11ShaderResourceView* textures[1] = { texture.Get() };

        deviceContext->PSSetShaderResources( 0, 1, textures );
      }

      // Set shaders and constant buffers.
      ApplyShaders( deviceContext, GetCurrentShaderPermutation() );
    }


    // Public constructor.
    InstancedBasicEffect::InstancedBasicEffect( _In_ ID3D11Device* device )
      : pImpl( new Impl( device ) )
    {
    }


    // Move constructor.
    InstancedBasicEffect::InstancedBasicEffect( InstancedBasicEffect&& moveFrom )
      : pImpl( std::move( moveFrom.pImpl ) )
    {
    }


    // Move assignment.
    InstancedBasicEffect& InstancedBasicEffect::operator= ( InstancedBasicEffect&& moveFrom )
    {
      pImpl = std::move( moveFrom.pImpl );
      return *this;
    }


    // Public destructor.
    InstancedBasicEffect::~InstancedBasicEffect()
    {
    }


    // IEffect methods.
    void InstancedBasicEffect::Apply( _In_ ID3D11DeviceContext* deviceContext )
    {
      pImpl->Apply( deviceContext );
    }


    void InstancedBasicEffect::GetVertexShaderBytecode( _Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength )
    {
      pImpl->GetVertexShaderBytecode( pImpl->GetCurrentShaderPermutation(), pShaderByteCode, pByteCodeLength );
    }


    // Camera settings.
    void XM_CALLCONV InstancedBasicEffect::SetWorld( FXMMATRIX value )
    {
      pImpl->matrices.world = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::WorldViewProj | EffectDirtyFlags::WorldInverseTranspose | EffectDirtyFlags::FogVector;
    }


    void XM_CALLCONV InstancedBasicEffect::SetView( FXMMATRIX view )
    {
      // Do nothing, instanced view is managed by DX::CameraResources
    }


    void XM_CALLCONV InstancedBasicEffect::SetProjection( FXMMATRIX projection )
    {
      // Do nothing, instanced view is managed by DX::CameraResources
    }


    void XM_CALLCONV InstancedBasicEffect::SetMatrices( FXMMATRIX world, CXMMATRIX view, CXMMATRIX projection )
    {
      pImpl->matrices.world = world;

      pImpl->dirtyFlags |= EffectDirtyFlags::WorldViewProj | EffectDirtyFlags::WorldInverseTranspose | EffectDirtyFlags::EyePosition | EffectDirtyFlags::FogVector;
    }

    // Material settings.
    void XM_CALLCONV InstancedBasicEffect::SetDiffuseColor( FXMVECTOR value )
    {
      pImpl->lights.diffuseColor = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    void XM_CALLCONV InstancedBasicEffect::SetEmissiveColor( FXMVECTOR value )
    {
      pImpl->lights.emissiveColor = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    void XM_CALLCONV InstancedBasicEffect::SetSpecularColor( FXMVECTOR value )
    {
      // Set xyz to new value, but preserve existing w (specular power).
      pImpl->constants.specularColorAndPower = XMVectorSelect( pImpl->constants.specularColorAndPower, value, g_XMSelect1110 );

      pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }


    void InstancedBasicEffect::SetSpecularPower( float value )
    {
      // Set w to new value, but preserve existing xyz (specular color).
      pImpl->constants.specularColorAndPower = XMVectorSetW( pImpl->constants.specularColorAndPower, value );

      pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }


    void InstancedBasicEffect::DisableSpecular()
    {
      // Set specular color to black, power to 1
      // Note: Don't use a power of 0 or the shader will generate strange highlights on non-specular materials

      pImpl->constants.specularColorAndPower = g_XMIdentityR3;

      pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }


    void InstancedBasicEffect::SetAlpha( float value )
    {
      pImpl->lights.alpha = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    void XM_CALLCONV InstancedBasicEffect::SetColorAndAlpha( FXMVECTOR value )
    {
      pImpl->lights.diffuseColor = value;
      pImpl->lights.alpha = XMVectorGetW( value );

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    // Light settings.
    void InstancedBasicEffect::SetLightingEnabled( bool value )
    {
      pImpl->lightingEnabled = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    void InstancedBasicEffect::SetPerPixelLighting( bool value )
    {
      pImpl->preferPerPixelLighting = value;
    }


    void XM_CALLCONV InstancedBasicEffect::SetAmbientLightColor( FXMVECTOR value )
    {
      pImpl->lights.ambientLightColor = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::MaterialColor;
    }


    void InstancedBasicEffect::SetLightEnabled( int whichLight, bool value )
    {
      pImpl->dirtyFlags |= pImpl->lights.SetLightEnabled( whichLight, value, pImpl->constants.lightDiffuseColor, pImpl->constants.lightSpecularColor );
    }


    void XM_CALLCONV InstancedBasicEffect::SetLightDirection( int whichLight, FXMVECTOR value )
    {
      EffectLights::ValidateLightIndex( whichLight );

      pImpl->constants.lightDirection[whichLight] = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }


    void XM_CALLCONV InstancedBasicEffect::SetLightDiffuseColor( int whichLight, FXMVECTOR value )
    {
      pImpl->dirtyFlags |= pImpl->lights.SetLightDiffuseColor( whichLight, value, pImpl->constants.lightDiffuseColor );
    }


    void XM_CALLCONV InstancedBasicEffect::SetLightSpecularColor( int whichLight, FXMVECTOR value )
    {
      pImpl->dirtyFlags |= pImpl->lights.SetLightSpecularColor( whichLight, value, pImpl->constants.lightSpecularColor );
    }


    void InstancedBasicEffect::EnableDefaultLighting()
    {
      EffectLights::EnableDefaultLighting( this );
    }

    // Fog settings.
    void InstancedBasicEffect::SetFogEnabled( bool value )
    {
      pImpl->fog.enabled = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::FogEnable;
    }


    void InstancedBasicEffect::SetFogStart( float value )
    {
      pImpl->fog.start = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::FogVector;
    }


    void InstancedBasicEffect::SetFogEnd( float value )
    {
      pImpl->fog.end = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::FogVector;
    }


    void XM_CALLCONV InstancedBasicEffect::SetFogColor( FXMVECTOR value )
    {
      pImpl->constants.fogColor = value;

      pImpl->dirtyFlags |= EffectDirtyFlags::ConstantBuffer;
    }


    // Vertex color setting.
    void InstancedBasicEffect::SetVertexColorEnabled( bool value )
    {
      pImpl->vertexColorEnabled = value;
    }


    // Texture settings.
    void InstancedBasicEffect::SetTextureEnabled( bool value )
    {
      pImpl->textureEnabled = value;
    }


    void InstancedBasicEffect::SetTexture( _In_opt_ ID3D11ShaderResourceView* value )
    {
      pImpl->texture = value;
    }
  }
}