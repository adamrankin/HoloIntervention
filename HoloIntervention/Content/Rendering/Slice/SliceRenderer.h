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
#include "IEngineComponent.h"
#include "SliceEntry.h"

// STL includes
#include <list>
#include <mutex>

// DirectX includes
#include <d3d11.h>

namespace DX
{
  class CameraResources;
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
  }

  namespace Rendering
  {
    struct VertexPositionTexture
    {
      DirectX::XMFLOAT3 pos;
      DirectX::XMFLOAT2 texCoord;
    };

    class SliceRenderer : public IEngineComponent
    {
      // list instead of vector so that erase does not require copy constructor
      typedef std::list<std::shared_ptr<SliceEntry>> SliceList;

    public:
      enum SliceOrigin
      {
        ORIGIN_CENTER,
        ORIGIN_TOPLEFT
      };

    public:
      SliceRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& timer);
      ~SliceRenderer();

      uint64 AddSlice(const std::wstring& fileName, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity(), bool headLocked = false);
      uint64 AddSlice(UWPOpenIGTLink::TrackedFrame^ frame, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity(), bool headLocked = false);
      uint64 AddSlice(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity(), bool headLocked = false);
      uint64 AddSlice(Windows::Storage::Streams::IBuffer^ imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity(), bool headLocked = false);
      void RemoveSlice(uint64 sliceToken);
      std::shared_ptr<SliceEntry> GetSlice(uint64 sliceToken);

      void UpdateSlice(uint64 sliceToken, std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity());
      void UpdateSlice(uint64 sliceToken, UWPOpenIGTLink::TrackedFrame^ frame, Windows::Foundation::Numerics::float4x4 desiredPose = Windows::Foundation::Numerics::float4x4::identity());

      void ShowSlice(uint64 sliceToken);
      void HideSlice(uint64 sliceToken);
      void SetSliceVisible(uint64 sliceToken, bool show);
      void SetSliceHeadlocked(uint64 sliceToken, bool headlocked);
      void SetSliceRenderOrigin(uint64 sliceToken, SliceOrigin origin);

      void ForceSlicePose(uint64 sliceToken, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float4x4 GetSlicePose(uint64 sliceToken) const;
      void SetDesiredSlicePose(uint64 sliceToken, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float3 GetSliceVelocity(uint64 sliceToken) const;

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ pose, const DX::CameraResources* cameraResources);
      void Render();

    protected:
      std::shared_ptr<SliceEntry> AddSliceCommon(const Windows::Foundation::Numerics::float4x4& desiredPose, bool headLocked);
      bool FindSlice(uint64 sliceToken, std::shared_ptr<SliceEntry>& sliceEntry) const;
      HRESULT CreateVertexBuffer(Microsoft::WRL::ComPtr<ID3D11Buffer>& comPtr, float bottom, float left, float right, float top);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;
      const DX::CameraResources*                          m_cameraResources = nullptr;
      DX::StepTimer&                                      m_timer;

      // Direct3D resources
      Microsoft::WRL::ComPtr<ID3D11InputLayout>           m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_indexBuffer;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>          m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>        m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>           m_colourPixelShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>           m_greyPixelShader;

      // Vertex buffers, one for image origin at top left, one for image origin at center
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_topLeftVertexBuffer;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_centerVertexBuffer;

      // Direct3D resources for the texture.
      Microsoft::WRL::ComPtr<ID3D11SamplerState>          m_quadTextureSamplerState;

      // System resources for quad geometry.
      uint32                                              m_indexCount = 0;
      bool                                                m_usingVprtShaders = false;

      // Lock protection when accessing image list
      mutable std::mutex                                  m_sliceMapMutex;
      SliceList                                           m_slices;
      uint64                                              m_nextUnusedSliceId = INVALID_TOKEN + 1;
    };
  }
}