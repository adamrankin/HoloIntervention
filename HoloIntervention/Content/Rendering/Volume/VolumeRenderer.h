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

// Local includes
#include "IEngineComponent.h"
#include "PiecewiseLinearTF.h"

namespace DX
{
  class CameraResources;
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    struct VolumeConstantBuffer
    {
      DirectX::XMFLOAT4X4                     worldMatrix;
      float                                   lt_maximumXValue;
      Windows::Foundation::Numerics::float3   stepSize;
      uint32                                  numIterations;
      Windows::Foundation::Numerics::float3   scaleFactor;
    };
    static_assert((sizeof(VolumeConstantBuffer) % (sizeof(float) * 4)) == 0, "Model constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    struct VertexPosition
    {
      Windows::Foundation::Numerics::float3 pos;
    };

    class VolumeRenderer : public IEngineComponent
    {
    public:
      enum TransferFunctionType
      {
        TransferFunction_Unknown,
        TransferFunction_Piecewise_Linear,
      };
      static const uint32_t INVALID_VOLUME_INDEX = 0;

    public:
      VolumeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~VolumeRenderer();

      void Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, DX::CameraResources* cameraResources, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);
      void Render();

      Concurrency::task<void> SetTransferFunctionTypeAsync(TransferFunctionType type, const std::vector<Windows::Foundation::Numerics::float2>& controlPoints);

      // D3D device related controls
      Concurrency::task<void> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();

    protected:
      void CreateVertexResources();
      void ReleaseVertexResources();
      void CreateCameraResources();
      void ReleaseCameraResources();
      void CreateTFResources();
      void ReleaseTFResources();

      void CreateVolumeResources();
      void ReleaseVolumeResources();
      void UpdateGPUImageData();

    protected:
      // Cached pointer to device and camera resources.
      std::shared_ptr<DX::DeviceResources>              m_deviceResources = nullptr;
      DX::CameraResources*                              m_cameraResources = nullptr;

      // Direct3D resources for volume rendering
      Microsoft::WRL::ComPtr<ID3D11InputLayout>         m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_vertexBuffer;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_indexBuffer;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>        m_volRenderVertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>      m_volRenderGeometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>         m_volRenderPixelShader;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_volumeConstantBuffer;
      Microsoft::WRL::ComPtr<ID3D11Texture3D>           m_volumeStagingTexture;
      Microsoft::WRL::ComPtr<ID3D11Texture3D>           m_volumeTexture;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_volumeSRV;
      Microsoft::WRL::ComPtr<ID3D11SamplerState>        m_samplerState;

      // D3D resources for left and right eye position calculation
      Microsoft::WRL::ComPtr<ID3D11PixelShader>         m_faceCalcPixelShader;
      Microsoft::WRL::ComPtr<ID3D11Texture2D>           m_frontPositionTextureArray;
      Microsoft::WRL::ComPtr<ID3D11Texture2D>           m_backPositionTextureArray;
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    m_frontPositionRTV;
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    m_backPositionRTV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_frontPositionSRV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_backPositionSRV;
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>     m_cullBackRasterState;
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>     m_cullFrontRasterState;

      // Transfer function GPU resources
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_lookupTableBuffer;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_lookupTableSRV;
      std::atomic_bool                                  m_tfResourcesReady = false;

      // IGT frame resources
      std::wstring                                      m_fromCoordFrame = L"Image";
      std::wstring                                      m_toCoordFrame = L"HMD";
      UWPOpenIGTLink::TransformName^                    m_imageToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_fromCoordFrame.c_str()), ref new Platform::String(m_toCoordFrame.c_str()));
      UWPOpenIGTLink::TransformRepository^              m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      UWPOpenIGTLink::TrackedFrame^                     m_frame = nullptr;

      // CPU resources for volume rendering
      uint32                                            m_indexCount = 0;
      VolumeConstantBuffer                              m_constantBuffer;
      uint16                                            m_frameSize[3] = { 0, 0, 0 };
      float                                             m_stepScale = 1.f;  // Increasing this reduces the number of steps taken per pixel

      // State flags
      std::atomic_bool                                  m_volumeReady = false;
      std::atomic_bool                                  m_faceCalcReady = false;
      std::atomic_bool                                  m_usingVprtShaders = false;

      // Transfer function CPU resources
      std::mutex                                        m_tfMutex;
      TransferFunctionType                              m_tfType = TransferFunction_Unknown;
      ITransferFunction*                                m_transferFunction = nullptr;
    };
  }
}