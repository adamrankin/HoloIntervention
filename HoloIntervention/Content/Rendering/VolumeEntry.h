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

#include "PiecewiseLinearTransferFunction.h"

// WinRt includes
#include <ppltasks.h>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    struct LookupTableBufferType
    {
      DirectX::XMFLOAT4                       lookupValue;
    };

    struct VolumeEntryConstantBuffer
    {
      DirectX::XMFLOAT4X4                     worldMatrix;
      DirectX::XMFLOAT3                       stepSize;
      float                                   lt_maximumXValue;
      uint32                                  lt_arraySize;
      uint32                                  numIterations;
      DirectX::XMFLOAT2                       buffer;
    };
    static_assert((sizeof(VolumeEntryConstantBuffer) % (sizeof(float) * 4)) == 0, "Volume constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    struct VertexPosition
    {
      Windows::Foundation::Numerics::float3 pos;
    };

    class VolumeEntry
    {
    public:
      typedef std::pair<float, Windows::Foundation::Numerics::float4> ControlPoint;
      typedef std::vector<ControlPoint> ControlPointList;
      enum TransferFunctionType
      {
        TransferFunction_Unknown,
        TransferFunction_Piecewise_Linear,
      };

    public:
      VolumeEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources,
                  uint64 token,
                  ID3D11Buffer* cwIndexBuffer,
                  ID3D11Buffer* ccwIndexBuffer,
                  ID3D11InputLayout* inputLayout,
                  ID3D11Buffer* vertexBuffer,
                  ID3D11VertexShader* volRenderVertexShader,
                  ID3D11GeometryShader* volRenderGeometryShader,
                  ID3D11PixelShader* volRenderPixelShader,
                  ID3D11PixelShader* faceCalcPixelShader,
                  ID3D11Texture2D* frontPositionTextureArray,
                  ID3D11Texture2D* backPositionTextureArray,
                  ID3D11RenderTargetView* frontPositionRTV,
                  ID3D11RenderTargetView* backPositionRTV,
                  ID3D11ShaderResourceView* frontPositionSRV,
                  ID3D11ShaderResourceView* backPositionSRV);
      ~VolumeEntry();

      void Update(const DX::StepTimer& timer);
      void Render(uint32 indexCount);

      void SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat);
      std::shared_ptr<byte> GetImageData() const;
      void SetShowing(bool showing);
      uint64 GetToken() const;

      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix);
      Windows::Foundation::Numerics::float3 GetVelocity() const;

      Concurrency::task<void> SetOpacityTransferFunctionTypeAsync(VolumeEntry::TransferFunctionType type, uint32 tableSize, const VolumeEntry::ControlPointList& controlPoints);

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      Windows::Foundation::Numerics::float4x4           m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4           m_currentPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4           m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float3             m_velocity = { 0.f, 0.f, 0.f };

    protected:
      void UpdateGPUImageData();

      void CreateVolumeResources();
      void ReleaseVolumeResources();
      void CreateTFResources();
      void ReleaseTFResources();

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>              m_deviceResources;

      // Cached pointers to re-used D3D resources
      ID3D11Buffer*                                     m_cwIndexBuffer;
      ID3D11Buffer*                                     m_ccwIndexBuffer;
      ID3D11InputLayout*                                m_inputLayout;
      ID3D11Buffer*                                     m_vertexBuffer;
      ID3D11VertexShader*                               m_volRenderVertexShader;
      ID3D11GeometryShader*                             m_volRenderGeometryShader;
      ID3D11PixelShader*                                m_volRenderPixelShader;
      ID3D11PixelShader*                                m_faceCalcPixelShader;

      // Direct3D resources for volume rendering
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_volumeEntryConstantBuffer;
      Microsoft::WRL::ComPtr<ID3D11Texture3D>           m_volumeStagingTexture;
      Microsoft::WRL::ComPtr<ID3D11Texture3D>           m_volumeTexture;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_volumeSRV;
      Microsoft::WRL::ComPtr<ID3D11SamplerState>        m_samplerState;

      // Cached D3D resources for left and right eye position calculation
      ID3D11Texture2D*                                  m_frontPositionTextureArray;
      ID3D11Texture2D*                                  m_backPositionTextureArray;
      ID3D11RenderTargetView*                           m_frontPositionRTV;
      ID3D11RenderTargetView*                           m_backPositionRTV;
      ID3D11ShaderResourceView*                         m_frontPositionSRV;
      ID3D11ShaderResourceView*                         m_backPositionSRV;

      // Transfer function GPU resources
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_opacityLookupTableBuffer;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_opacityLookupTableSRV;
      std::atomic_bool                                  m_tfResourcesReady = false;

      // Transfer function CPU resources
      mutable std::mutex                                m_opacityTFMutex;
      TransferFunctionType                              m_opacityTFType = TransferFunction_Unknown;
      BaseTransferFunction*                             m_opacityTransferFunction = nullptr;

      // CPU resources for volume rendering
      VolumeEntryConstantBuffer                         m_constantBuffer;
      std::shared_ptr<byte>                             m_imageData = nullptr;
      std::shared_ptr<byte>                             m_onGPUImageData = nullptr;
      uint16                                            m_frameSize[3] = { 0, 0, 0 };
      mutable std::mutex                                m_imageAccessMutex;
      DXGI_FORMAT                                       m_pixelFormat = DXGI_FORMAT_UNKNOWN;
      float                                             m_stepScale = 1.f;  // Increasing this reduces the number of steps taken per pixel

      // State
      uint64                                            m_token = 0;
      std::atomic_bool                                  m_showing = true;
      std::atomic_bool                                  m_entryReady = false;
      std::atomic_bool                                  m_volumeReady = false;
      std::atomic_bool                                  m_volumeUpdateNeeded = false;

      // Constants relating to volume entry behavior
      static const float                                LERP_RATE;

    };
  }
}