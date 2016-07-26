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

#pragma once

// DirectXTK includes
#include <EffectCommon.h>
#include <ConstantBuffer.h>

namespace DirectX
{
  struct EffectStereoMatrices
  {
    EffectStereoMatrices();

    XMMATRIX world;
    XMMATRIX view[2];
    XMMATRIX projection[2];
    XMMATRIX worldView[2];

    void SetConstants( _Inout_ int& dirtyFlags, _Inout_ XMMATRIX* worldViewProjConstant[2] );
  };

  struct EffectStereoFog
  {
    EffectStereoFog();

    bool enabled;
    float start;
    float end;

    void XM_CALLCONV SetConstants( _Inout_ int& dirtyFlags, _In_ const XMMATRIX* worldView[2], _Inout_ XMVECTOR fogVectorConstant[2] );
  };

  struct EffectStereoLights : public EffectColor
  {
    EffectStereoLights();

    static const int MaxDirectionalLights = IEffectLights::MaxDirectionalLights;

    // Fields.
    XMVECTOR emissiveColor;
    XMVECTOR ambientLightColor;

    bool lightEnabled[MaxDirectionalLights];
    XMVECTOR lightDiffuseColor[MaxDirectionalLights];
    XMVECTOR lightSpecularColor[MaxDirectionalLights];

    // Methods.
    void InitializeConstants( _Out_ XMVECTOR& specularColorAndPowerConstant,
                              _Out_writes_all_( MaxDirectionalLights )
                              XMVECTOR* lightDirectionConstant,
                              _Out_writes_all_( MaxDirectionalLights ) XMVECTOR* lightDiffuseConstant,
                              _Out_writes_all_( MaxDirectionalLights ) XMVECTOR* lightSpecularConstant );
    void SetConstants( _Inout_ int& dirtyFlags,
                       _In_ EffectStereoMatrices const& matrices,
                       _Inout_ XMMATRIX& worldConstant,
                       _Inout_updates_( 3 ) XMVECTOR worldInverseTransposeConstant[3],
                       _Inout_ XMVECTOR* eyePositionConstant[2],
                       _Inout_ XMVECTOR& diffuseColorConstant,
                       _Inout_ XMVECTOR& emissiveColorConstant,
                       bool lightingEnabled );
    int SetLightEnabled( int whichLight,
                         bool value,
                         _Inout_updates_( MaxDirectionalLights ) XMVECTOR* lightDiffuseConstant,
                         _Inout_updates_( MaxDirectionalLights ) XMVECTOR* lightSpecularConstant );
    int XM_CALLCONV SetLightDiffuseColor( int whichLight,
                                          FXMVECTOR value,
                                          _Inout_updates_( MaxDirectionalLights ) XMVECTOR* lightDiffuseConstant );
    int XM_CALLCONV SetLightSpecularColor( int whichLight,
                                           FXMVECTOR value,
                                           _Inout_updates_( MaxDirectionalLights ) XMVECTOR* lightSpecularConstant );

    static void ValidateLightIndex( int whichLight );
    static void EnableDefaultLighting( _In_ IEffectLights* effect );
  };

  template<typename Traits>
  class InstancedEffectBase : public AlignedNew<typename Traits::ConstantBufferType>
  {
  public:
    // Constructor.
    InstancedEffectBase( _In_ ID3D11Device* device );

    // Fields.
    typename Traits::ConstantBufferType constants;

    EffectStereoMatrices matrices;
    EffectStereoFog fog;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;

    int dirtyFlags;

    // Helper looks up the bytecode for the specified vertex shader permutation.
    // Client code needs this in order to create matching input layouts.
    void GetVertexShaderBytecode( int permutation, _Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength );

    // Helper sets our shaders and constant buffers onto the D3D device.
    void ApplyShaders( _In_ ID3D11DeviceContext* deviceContext, int permutation );

    // Helper returns the default texture.
    ID3D11ShaderResourceView* GetDefaultTexture() { return mDeviceResources->GetDefaultTexture(); }

  protected:
    // Static arrays hold all the precompiled shader permutations.
    static const ShaderBytecode VertexShaderBytecode[Traits::VertexShaderCount];
    static const ShaderBytecode PixelShaderBytecode[Traits::PixelShaderCount];

    static const int VertexShaderIndices[Traits::ShaderPermutationCount];
    static const int PixelShaderIndices[Traits::ShaderPermutationCount];

  private:
    // D3D constant buffer holds a copy of the same data as the public 'constants' field.
    ConstantBuffer<typename Traits::ConstantBufferType> mConstantBuffer;

    // Only one of these helpers is allocated per D3D device, even if there are multiple effect instances.
    class DeviceResources : protected EffectDeviceResources
    {
    public:
      DeviceResources( _In_ ID3D11Device* device )
        : EffectDeviceResources( device )
      { }


      // Gets or lazily creates the specified vertex shader permutation.
      ID3D11VertexShader* GetVertexShader( int permutation )
      {
        int shaderIndex = VertexShaderIndices[permutation];

        return DemandCreateVertexShader( mVertexShaders[shaderIndex], VertexShaderBytecode[shaderIndex] );
      }


      // Gets or lazily creates the specified pixel shader permutation.
      ID3D11PixelShader* GetPixelShader( int permutation )
      {
        int shaderIndex = PixelShaderIndices[permutation];

        return DemandCreatePixelShader( mPixelShaders[shaderIndex], PixelShaderBytecode[shaderIndex] );
      }


      // Gets or lazily creates the default texture
      ID3D11ShaderResourceView* GetDefaultTexture() { return EffectDeviceResources::GetDefaultTexture(); }


    private:
      Microsoft::WRL::ComPtr<ID3D11VertexShader> mVertexShaders[Traits::VertexShaderCount];
      Microsoft::WRL::ComPtr<ID3D11PixelShader> mPixelShaders[Traits::PixelShaderCount];
    };


    // Per-device resources.
    std::shared_ptr<DeviceResources> mDeviceResources;

    static SharedResourcePool<ID3D11Device*, DeviceResources> deviceResourcesPool;
  };

  //----------------------------------------------------------------------------
  template<typename Traits>
  DirectX::InstancedEffectBase<Traits>::InstancedEffectBase( _In_ ID3D11Device* device ) : dirtyFlags( INT_MAX ),
    mConstantBuffer( device ),
    mDeviceResources( deviceResourcesPool.DemandCreate( device ) ),
    constants{}
  {

  }

  //----------------------------------------------------------------------------
  template<typename Traits>
  void DirectX::InstancedEffectBase<Traits>::GetVertexShaderBytecode( int permutation, _Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength )
  {
    int shaderIndex = VertexShaderIndices[permutation];

    ShaderBytecode const& bytecode = VertexShaderBytecode[shaderIndex];

    *pShaderByteCode = bytecode.code;
    *pByteCodeLength = bytecode.length;
  }

  //----------------------------------------------------------------------------
  template<typename Traits>
  void DirectX::InstancedEffectBase<Traits>::ApplyShaders( _In_ ID3D11DeviceContext* deviceContext, int permutation )
  {
    // Set shaders.
    auto vertexShader = mDeviceResources->GetVertexShader( permutation );
    auto pixelShader = mDeviceResources->GetPixelShader( permutation );

    deviceContext->VSSetShader( vertexShader, nullptr, 0 );
    deviceContext->PSSetShader( pixelShader, nullptr, 0 );

    // Make sure the constant buffer is up to date.
    if ( dirtyFlags & EffectDirtyFlags::ConstantBuffer )
    {
      mConstantBuffer.SetData( deviceContext, constants );

      dirtyFlags &= ~EffectDirtyFlags::ConstantBuffer;
    }

    // Set the constant buffer.
    ID3D11Buffer* buffer = mConstantBuffer.GetBuffer();

    deviceContext->VSSetConstantBuffers( 0, 1, &buffer );
    deviceContext->PSSetConstantBuffers( 0, 1, &buffer );
  }
}