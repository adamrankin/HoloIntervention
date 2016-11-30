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
#include "SliceEntry.h"

// STL includes
#include <list>

// DirectX includes
#include <d3d11.h>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class SliceRenderer : public IEngineComponent
    {
      // list instead of vector so that erase does not require copy constructor
      typedef std::list<std::shared_ptr<SliceEntry>> SliceList;

    public:
      SliceRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~SliceRenderer();

      uint32 AddSlice();
      uint32 AddSlice(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 imageToTrackerTransform, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);
      uint32 AddSlice(Windows::Storage::Streams::IBuffer^ imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 imageToTrackerTransform, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);
      void RemoveSlice(uint32 sliceId);

      void UpdateSlice(uint32 sliceId, std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, Windows::Foundation::Numerics::float4x4 imageToTrackerTransform, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

      void ShowSlice(uint32 sliceId);
      void HideSlice(uint32 sliceId);
      void SetSliceVisible(uint32 sliceId, bool show);
      void SetSliceHeadlocked(uint32 sliceId, bool headlocked);

      void SetSlicePose(uint32 sliceId, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float4x4 GetSlicePose(uint32 sliceId) const;
      void SetDesiredSlicePose(uint32 sliceId, const Windows::Foundation::Numerics::float4x4& pose);
      Windows::Foundation::Numerics::float3 GetSliceVelocity(uint32 sliceId) const;

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ pose, const DX::StepTimer& timer);
      void Render();

      static const uint32_t INVALID_SLICE_INDEX = 0;

    protected:
      bool FindSlice(uint32 sliceId, std::shared_ptr<SliceEntry>& sliceEntry) const;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;

      // Direct3D resources
      Microsoft::WRL::ComPtr<ID3D11InputLayout>           m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_indexBuffer;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>          m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>        m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>           m_pixelShader;

      // Direct3D resources for the texture.
      Microsoft::WRL::ComPtr<ID3D11SamplerState>          m_quadTextureSamplerState;

      // System resources for quad geometry.
      uint32                                              m_indexCount = 0;
      bool                                                m_usingVprtShaders = false;

      // Lock protection when accessing image list
      mutable std::mutex                                  m_sliceMapMutex;
      SliceList                                           m_slices;
      uint32                                              m_nextUnusedSliceId = 1; // start at 1, 0 is considered invalid
    };
  }
}