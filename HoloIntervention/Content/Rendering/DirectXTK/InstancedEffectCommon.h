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

#pragma once

// Local includes
#include "InstancedEffects.h"

// DirectXTK includes
#include <ConstantBuffer.h>
#include <DemandCreate.h>
#include <DirectXMath.h>
#include <EffectCommon.h>

namespace DirectX
{
  // Helper stores matrix parameter values, and computes derived matrices.
  struct StereoEffectMatrices
  {
    StereoEffectMatrices();

    XMMATRIX world;
    XMMATRIX view[2];
    XMMATRIX projection[2];
    XMMATRIX worldView[2];

    void SetConstants(_Inout_ int& dirtyFlags, _Inout_ XMMATRIX& leftWorldViewProjConstant, _Inout_ XMMATRIX& rightWorldViewProjConstant);
  };


  // Helper stores the current fog settings, and computes derived shader parameters.
  struct StereoEffectFog
  {
    StereoEffectFog();

    bool enabled;
    float start;
    float end;

    void XM_CALLCONV SetConstants(_Inout_ int& dirtyFlags, _In_ FXMMATRIX leftWorldView, _In_ FXMMATRIX rightWorldView, _Inout_ XMVECTOR& leftFogVectorConstant, _Inout_ XMVECTOR& rightFogVectorConstant);
  };

  struct StereoEffectLights : public EffectColor
  {
    StereoEffectLights();

    static const int MaxDirectionalLights = IEffectLights::MaxDirectionalLights;

    // Fields.
    XMVECTOR emissiveColor;
    XMVECTOR ambientLightColor;

    bool lightEnabled[MaxDirectionalLights];
    XMVECTOR lightDiffuseColor[MaxDirectionalLights];
    XMVECTOR lightSpecularColor[MaxDirectionalLights];

    // Methods.
    void InitializeConstants(_Out_ XMVECTOR& specularColorAndPowerConstant,
                             _Out_writes_all_(MaxDirectionalLights)
                             XMVECTOR* lightDirectionConstant,
                             _Out_writes_all_(MaxDirectionalLights) XMVECTOR* lightDiffuseConstant,
                             _Out_writes_all_(MaxDirectionalLights) XMVECTOR* lightSpecularConstant);
    void SetConstants(_Inout_ int& dirtyFlags,
                      _In_ StereoEffectMatrices const& matrices,
                      _Inout_ XMMATRIX& worldConstant,
                      _Inout_updates_(3) XMVECTOR worldInverseTransposeConstant[3],
                      _Inout_ XMVECTOR& leftEyePositionConstant,
                      _Inout_ XMVECTOR& rightEyePositionConstant,
                      _Inout_ XMVECTOR& diffuseColorConstant,
                      _Inout_ XMVECTOR& emissiveColorConstant,
                      bool lightingEnabled);
    int SetLightEnabled(int whichLight,
                        bool value,
                        _Inout_updates_(MaxDirectionalLights) XMVECTOR* lightDiffuseConstant,
                        _Inout_updates_(MaxDirectionalLights) XMVECTOR* lightSpecularConstant);
    int XM_CALLCONV SetLightDiffuseColor(int whichLight,
                                         FXMVECTOR value,
                                         _Inout_updates_(MaxDirectionalLights) XMVECTOR* lightDiffuseConstant);
    int XM_CALLCONV SetLightSpecularColor(int whichLight,
                                          FXMVECTOR value,
                                          _Inout_updates_(MaxDirectionalLights) XMVECTOR* lightSpecularConstant);

    static void ValidateLightIndex(int whichLight);
    static void EnableDefaultLighting(_In_ IEffectLights* effect);
  };

  template<typename Traits>
  class InstancedEffectBase : public AlignedNew<typename Traits::ConstantBufferType>
  {
  public:
    // Constructor.
    InstancedEffectBase(_In_ ID3D11Device* device);

    // Fields.
    typename Traits::ConstantBufferType constants;

    StereoEffectMatrices matrices;
    StereoEffectFog fog;

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> texture;

    int dirtyFlags;

    // Helper looks up the bytecode for the specified vertex shader permutation.
    // Client code needs this in order to create matching input layouts.
    void GetVertexShaderBytecode(int permutation, _Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength);

    // Helper sets our shaders and constant buffers onto the D3D device.
    void ApplyShaders(_In_ ID3D11DeviceContext* deviceContext, int permutation);

    // Helper returns the default texture.
    ID3D11ShaderResourceView* GetDefaultTexture()
    {
      return mDeviceResources->GetDefaultTexture();
    }

  protected:
    // Static arrays hold all the precompiled shader permutations that support setting of render target array at any stage of the pipeline.
    static const ShaderBytecode VPRTVertexShaderBytecode[Traits::VertexShaderCount];

    // Static arrays hold all the precompiled shader permutations.
    static const ShaderBytecode VertexShaderBytecode[Traits::VertexShaderCount];
    static const ShaderBytecode GeometryShaderBytecode[Traits::GeometryShaderCount];
    static const ShaderBytecode PixelShaderBytecode[Traits::PixelShaderCount];

    static const int VertexShaderIndices[Traits::ShaderPermutationCount];
    static const int GeometryShaderIndices[Traits::ShaderPermutationCount];
    static const int PixelShaderIndices[Traits::ShaderPermutationCount];

  private:
    // D3D constant buffer holds a copy of the same data as the public 'constants' field.
    ConstantBuffer<typename Traits::ConstantBufferType> mConstantBuffer;

    // Whether or not the given device supports VPRT
    bool m_supportsVPRT = false;

    // Only one of these helpers is allocated per D3D device, even if there are multiple effect instances.
    class DeviceResources : protected EffectDeviceResources
    {
    public:
      DeviceResources(_In_ ID3D11Device* device)
        : EffectDeviceResources(device)
      {
        // Check for device support for the optional feature that allows setting the render target array index from the vertex shader stage.
        D3D11_FEATURE_DATA_D3D11_OPTIONS3 options;
        device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof(options));
        if (options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer)
        {
          m_supportsVPRT = true;
        }
      }

      // Gets or lazily creates the specified vertex shader permutation.
      ID3D11GeometryShader* DeviceResources::DemandCreateGeometryShader(_Inout_ Microsoft::WRL::ComPtr<ID3D11GeometryShader>& geometryShader, ShaderBytecode const& bytecode)
      {
        return DemandCreate(geometryShader, mMutex, [&](ID3D11GeometryShader** pResult) -> HRESULT
        {
          HRESULT hr = mDevice->CreateGeometryShader(bytecode.code, bytecode.length, nullptr, pResult);

          if (SUCCEEDED(hr))
            SetDebugObjectName(*pResult, "DirectXTK:GSEffect");

          return hr;
        });
      }

      // Gets or lazily creates the specified vertex shader permutation.
      ID3D11VertexShader* GetVertexShader(int permutation)
      {
        int shaderIndex = VertexShaderIndices[permutation];

        return DemandCreateVertexShader(m_supportsVPRT ? mVPRTVertexShaders[shaderIndex] : mVertexShaders[shaderIndex],
                                        m_supportsVPRT ? VPRTVertexShaderBytecode[shaderIndex] : VertexShaderBytecode[shaderIndex]);
      }

      // Gets or lazily creates the specified vertex shader permutation.
      ID3D11GeometryShader* GetGeometryShader(int permutation)
      {
        int shaderIndex = GeometryShaderIndices[permutation];

        return DemandCreateGeometryShader(mGeometryShaders[shaderIndex], GeometryShaderBytecode[shaderIndex]);
      }

      // Gets or lazily creates the specified pixel shader permutation.
      ID3D11PixelShader* GetPixelShader(int permutation)
      {
        int shaderIndex = PixelShaderIndices[permutation];

        return DemandCreatePixelShader(mPixelShaders[shaderIndex], PixelShaderBytecode[shaderIndex]);
      }

      // Gets or lazily creates the default texture
      ID3D11ShaderResourceView* GetDefaultTexture()
      {
        return EffectDeviceResources::GetDefaultTexture();
      }

      bool GetSupportsVPRT() const { return m_supportsVPRT; }

    private:
      bool m_supportsVPRT = false;
      Microsoft::WRL::ComPtr<ID3D11VertexShader> mVPRTVertexShaders[Traits::VertexShaderCount];
      Microsoft::WRL::ComPtr<ID3D11VertexShader> mVertexShaders[Traits::VertexShaderCount];
      Microsoft::WRL::ComPtr<ID3D11GeometryShader> mGeometryShaders[Traits::GeometryShaderCount];
      Microsoft::WRL::ComPtr<ID3D11PixelShader> mPixelShaders[Traits::PixelShaderCount];
    };

    // Per-device resources.
    std::shared_ptr<DeviceResources> mDeviceResources;

    static SharedResourcePool<ID3D11Device*, DeviceResources> deviceResourcesPool;
  };

  //----------------------------------------------------------------------------
  template<typename Traits>
  DirectX::InstancedEffectBase<Traits>::InstancedEffectBase(_In_ ID3D11Device* device) : dirtyFlags(INT_MAX),
    mConstantBuffer(device),
    mDeviceResources(deviceResourcesPool.DemandCreate(device)),
    constants{}
  {
    // Check for device support for the optional feature that allows setting the render target array index from the vertex shader stage.
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options;
    device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof(options));
    if (options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer)
    {
      m_supportsVPRT = true;
    }
  }

  //----------------------------------------------------------------------------
  template<typename Traits>
  void DirectX::InstancedEffectBase<Traits>::GetVertexShaderBytecode(int permutation, _Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength)
  {
    int shaderIndex = VertexShaderIndices[permutation];
    ShaderBytecode const& bytecode = m_supportsVPRT ? VPRTVertexShaderBytecode[shaderIndex] : VertexShaderBytecode[shaderIndex];

    *pShaderByteCode = bytecode.code;
    *pByteCodeLength = bytecode.length;
  }

  //----------------------------------------------------------------------------
  template<typename Traits>
  void DirectX::InstancedEffectBase<Traits>::ApplyShaders(_In_ ID3D11DeviceContext* deviceContext, int permutation)
  {
    // Set shaders.
    auto vertexShader = mDeviceResources->GetVertexShader(permutation);
    deviceContext->VSSetShader(vertexShader, nullptr, 0);

    if (!mDeviceResources->GetSupportsVPRT())
    {
      auto geometryShader = mDeviceResources->GetGeometryShader(permutation);
      deviceContext->GSSetShader(geometryShader, nullptr, 0);
    }

    auto pixelShader = mDeviceResources->GetPixelShader(permutation);
    deviceContext->PSSetShader(pixelShader, nullptr, 0);

    // Make sure the constant buffer is up to date.
    if (dirtyFlags & EffectDirtyFlags::ConstantBuffer)
    {
      mConstantBuffer.SetData(deviceContext, constants);

      dirtyFlags &= ~EffectDirtyFlags::ConstantBuffer;
    }

    // Set the constant buffer.
    ID3D11Buffer* buffer = mConstantBuffer.GetBuffer();

    deviceContext->VSSetConstantBuffers(0, 1, &buffer);
    if (!mDeviceResources->GetSupportsVPRT())
    {
      deviceContext->GSSetConstantBuffers(0, 1, &buffer);
    }
    deviceContext->PSSetConstantBuffers(0, 1, &buffer);
  }
}
