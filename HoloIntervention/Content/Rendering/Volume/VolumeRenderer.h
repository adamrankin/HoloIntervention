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
#include "VolumeEntry.h"

// WinRt includes
#include <ppltasks.h>

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
    struct VolumeRendererConstantBuffer
    {
      DirectX::XMFLOAT4     viewportDimensions;
    };
    static_assert((sizeof(VolumeRendererConstantBuffer) % (sizeof(float) * 4)) == 0, "Volume constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    class VolumeRenderer : public IEngineComponent
    {
      typedef std::list<std::shared_ptr<VolumeEntry>> VolumeList;

    public:
      VolumeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~VolumeRenderer();

      uint64 AddVolume(std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 desiredPose);
      uint64 AddVolume(UWPOpenIGTLink::TrackedFrame^ frame, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity());
      void RemoveVolume(uint64 volumeToken);
      std::shared_ptr<VolumeEntry> GetVolume(uint64 volumeToken);

      void UpdateVolume(uint64 volumeToken, std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 desiredPose);
      void UpdateVolume(uint64 volumeToken, UWPOpenIGTLink::TrackedFrame^ frame, Windows::Foundation::Numerics::float4x4 desiredPose);

      void ShowVolume(uint64 volumeToken);
      void HideVolume(uint64 volumeToken);
      void SetVolumeVisible(uint64 volumeToken, bool show);

      void SetVolumePose(uint64 volumeToken, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float4x4 GetVolumePose(uint64 volumeToken) const;
      void SetDesiredVolumePose(uint64 volumeToken, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float3 GetVolumeVelocity(uint64 volumeToken) const;

      void Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, DX::CameraResources* cameraResources, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);
      void Render();

      // D3D device related controls
      Concurrency::task<void> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();

    protected:
      bool FindVolume(uint64 volumeToken, std::shared_ptr<VolumeEntry>& volumeEntry) const;

    protected:
      void CreateVertexResources();
      void ReleaseVertexResources();
      void CreateCameraResources();
      void ReleaseCameraResources();

    protected:
      // Cached pointer to device and camera resources.
      std::shared_ptr<DX::DeviceResources>              m_deviceResources = nullptr;
      DX::CameraResources*                              m_cameraResources = nullptr;

      // Direct3D resources for volume rendering
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_cwIndexBuffer;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_ccwIndexBuffer;
      Microsoft::WRL::ComPtr<ID3D11InputLayout>         m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_vertexBuffer;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>        m_volRenderVertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>      m_volRenderGeometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>         m_volRenderPixelShader;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_volumeRendererConstantBuffer;

      // D3D resources for left and right eye position calculation
      Microsoft::WRL::ComPtr<ID3D11Texture2D>           m_frontPositionTextureArray;
      Microsoft::WRL::ComPtr<ID3D11Texture2D>           m_backPositionTextureArray;
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    m_frontPositionRTV;
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    m_backPositionRTV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_frontPositionSRV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_backPositionSRV;

      // D3D resources for left and right eye position calculation
      Microsoft::WRL::ComPtr<ID3D11PixelShader>         m_faceCalcPixelShader;

      VolumeRendererConstantBuffer                      m_constantBuffer;
      std::atomic_bool                                  m_verticesReady = false;
      std::atomic_bool                                  m_cameraResourcesReady = false;
      uint32                                            m_indexCount = 0;
      mutable std::mutex                                m_volumeMapMutex;
      VolumeList                                        m_volumes;
      uint64                                            m_nextUnusedVolumeToken = INVALID_TOKEN + 1;
      std::atomic_bool                                  m_usingVprtShaders = false;
    };
  }
}