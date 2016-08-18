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

namespace HoloIntervention
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

    static_assert((sizeof(SliceConstantBuffer) % (sizeof(float) * 4)) == 0, "SliceConstantBuffer constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    class SliceEntry
    {
    public:
      SliceEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SliceEntry();

      void Update( SpatialPointerPose^ pose, const DX::StepTimer& timer );
      void Render( uint32 indexCount );

      void SetImageData( std::shared_ptr<byte*> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat );
      std::shared_ptr<byte*> GetImageData() const;

      void SetDesiredPose( const DirectX::XMFLOAT4X4& matrix );

      void SetHeadlocked(bool headLocked);

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      uint32                                m_id;
      SliceConstantBuffer                   m_constantBuffer;
      bool                                  m_showing;
      SimpleMath::Matrix                    m_desiredPose; // Poses in column-major format
      SimpleMath::Matrix                    m_currentPose;
      SimpleMath::Matrix                    m_lastPose;
      DXGI_FORMAT                           m_pixelFormat;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>  m_deviceResources;

      ComPtr<ID3D11Texture2D>               m_texture;
      ComPtr<ID3D11ShaderResourceView>      m_shaderResourceView;
      ComPtr<ID3D11Buffer>                  m_vertexBuffer;
      ComPtr<ID3D11Buffer>                  m_sliceConstantBuffer;
      
      // Rendering behavior vars
      bool                                  m_headLocked;
      float                                 m_scalingFactor;

      // image data vars
      std::shared_ptr<byte*>                m_imageData;
      uint16                                m_width;
      uint16                                m_height;
      std::mutex                            m_imageAccessMutex;

      // Constants relating to slice renderer behavior
      static const float3                   LOCKED_SLICE_SCREEN_OFFSET;
      static const float                    LOCKED_SLICE_DISTANCE_OFFSET;
      static const float                    LOCKED_SLICE_SCALE_FACTOR;
      static const float                    LERP_RATE;

    };
  }
}