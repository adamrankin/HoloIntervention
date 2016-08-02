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

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    class SliceRenderer
    {
      // list instead of vector so that erase does not require copy constructor
      typedef std::list<SliceEntry> SliceList;

    public:
      SliceRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SliceRenderer();

      uint32 AddSlice( byte* imageData, uint32 width, uint32 height );
      void RemoveSlice( uint32 sliceId );

      void ShowSlice( uint32 sliceId );
      void HideSlice( uint32 sliceId );
      void SetSliceVisible( uint32 sliceId, bool show );

      // Hard set of the slice pose, slice will jump to the given pose
      void SetSlicePose( uint32 sliceId, const XMFLOAT4X4& pose );

      // Set the target slice pose, system will smoothly animate the slice to that position
      void SetDesiredSlicePose( uint32 sliceId, const XMFLOAT4X4& pose );

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update( const DX::StepTimer& timer );
      void Render();

    protected:
      bool FindSlice( uint32 sliceId, SliceEntry*& sliceEntry );

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
      uint32                                              m_nextUnusedSliceId = 0;
    };
  }
}