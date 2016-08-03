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

// DirectXTK includes
#include <SimpleMath.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    struct VertexPositionTexture
    {
      XMFLOAT3 pos;
      XMFLOAT2 texCoord;
    };

    struct SliceConstantBuffer
    {
      XMFLOAT4X4 worldMatrix;
    };

    class SliceEntry
    {
    public:
      SliceEntry( uint32 width, uint32 height );
      ~SliceEntry();

      void Update( SpatialPointerPose^ pose, const DX::StepTimer& timer );
      void Render( uint32 indexCount );

      void SetImageData( byte* imageData );
      byte* GetImageData() const;

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      uint32                                m_id;
      uint32                                m_width;
      uint32                                m_height;
      SliceConstantBuffer                   m_constantBuffer;
      bool                                  m_showing;
      bool                                  m_headLocked;
      SimpleMath::Matrix                    m_desiredPose;
      SimpleMath::Matrix                    m_currentPose;
      SimpleMath::Matrix                    m_lastPose;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>  m_deviceResources;

      ComPtr<ID3D11Texture2D>               m_texture;
      ComPtr<ID3D11ShaderResourceView>      m_shaderResourceView;
      ComPtr<ID3D11Buffer>                  m_vertexBuffer;
      ComPtr<ID3D11Buffer>                  m_sliceConstantBuffer;
      bool                                  m_loadingComplete;

      // image data vars
      byte*                                 m_imageData;

      // Constants relating to slice renderer behavior
      static const float3                                 LOCKED_SLICE_SCREEN_OFFSET;
      static const float                                  LOCKED_SLICE_DISTANCE_OFFSET;
      static const float                                  LERP_RATE;
    };
  }
}