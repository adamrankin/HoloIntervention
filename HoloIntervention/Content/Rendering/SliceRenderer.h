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
#include "StepTimer.h"
#include "DeviceResources.h"
#include "SliceEntry.h"

// Windows includes
#include <ppltasks.h>

// DirectXTK includes
#include <SimpleMath.h>

// STD includes
#include <list>

// DirectX includes
#include <d3d11.h>

// Sound includes
#include "IVoiceInput.h"

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    class SliceRenderer : public Sound::IVoiceInput
    {
      // list instead of vector so that erase does not require copy constructor
      typedef std::list<std::shared_ptr<SliceEntry>> SliceList;

    public:
      SliceRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SliceRenderer();

      uint32 AddSlice();
      uint32 AddSlice( std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 embeddedImageTransform );
      uint32 AddSlice( IBuffer^ imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 embeddedImageTransform );
      void RemoveSlice( uint32 sliceId );

      void UpdateSlice( uint32 sliceId, std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 embeddedImageTransform );

      void ShowSlice( uint32 sliceId );
      void HideSlice( uint32 sliceId );
      void SetSliceVisible( uint32 sliceId, bool show );
      void SetSliceHeadlocked( uint32 sliceId, bool headlocked );

      // Hard set of the slice pose, slice will jump to the given pose
      void SetSlicePose( uint32 sliceId, const float4x4& pose );

      // For holographic system stabilization, the pose of a slice is needed
      bool GetSlicePose( uint32 sliceId, float4x4& outPose );

      // Set the target slice pose, system will smoothly animate the slice to that position
      void SetDesiredSlicePose( uint32 sliceId, const float4x4& pose );

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update( SpatialPointerPose^ pose, const DX::StepTimer& timer );
      void Render();

      // IVoiceInput
      virtual void RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg );

    protected:
      bool FindSlice( uint32 sliceId, std::shared_ptr<SliceEntry>& sliceEntry );

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;

      // Direct3D resources
      ComPtr<ID3D11InputLayout>                           m_inputLayout;
      ComPtr<ID3D11Buffer>                                m_indexBuffer;
      ComPtr<ID3D11VertexShader>                          m_vertexShader;
      ComPtr<ID3D11GeometryShader>                        m_geometryShader;
      ComPtr<ID3D11PixelShader>                           m_pixelShader;

      // Direct3D resources for the texture.
      ComPtr<ID3D11SamplerState>                          m_quadTextureSamplerState;

      // System resources for quad geometry.
      uint32                                              m_indexCount = 0;

      // Variables used with the rendering loop.
      bool                                                m_loadingComplete = false;

      // If the current D3D Device supports VPRT, we can avoid using a geometry
      // shader just to set the render target array index.
      bool                                                m_usingVprtShaders = false;

      // Lock protection when accessing image list
      std::mutex                                          m_sliceMapMutex;
      SliceList                                           m_slices;
      uint32                                              m_nextUnusedSliceId = 1; // start at 1, 0 is considered invalid
    };
  }
}