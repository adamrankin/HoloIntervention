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

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    struct VertexPositionTexture
    {
      DirectX::XMFLOAT3 pos;
      DirectX::XMFLOAT2 texCoord;
    };

    struct SliceConstantBuffer
    {
      DirectX::XMFLOAT4X4 worldMatrix;
    };

    static_assert((sizeof(SliceConstantBuffer) % (sizeof(float) * 4)) == 0, "SliceConstantBuffer constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    class SliceEntry
    {
    public:
      SliceEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~SliceEntry();

      void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ pose, const DX::StepTimer& timer);
      void Render(uint32 indexCount);

      void SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat);
      std::shared_ptr<byte> GetImageData() const;

      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix);

      void SetHeadlocked(bool headLocked);

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      uint32                                              m_id = 0;
      SliceConstantBuffer                                 m_constantBuffer;
      std::atomic_bool                                    m_showing = true;
      Windows::Foundation::Numerics::float4x4             m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_currentPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      DXGI_FORMAT                                         m_pixelFormat = DXGI_FORMAT_UNKNOWN;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;

      Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_imageTexture;
      Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_imageStagingTexture;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_shaderResourceView;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_vertexBuffer;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_sliceConstantBuffer;

      // Rendering behavior vars
      std::atomic_bool                                    m_headLocked = false;
      float                                               m_scalingFactor = 1.f;

      // image data vars
      std::shared_ptr<byte>                               m_imageData = nullptr;
      uint16                                              m_width = 0;
      uint16                                              m_height = 0;
      std::mutex                                          m_imageAccessMutex;

      // Constants relating to slice renderer behavior
      static const Windows::Foundation::Numerics::float3  LOCKED_SLICE_SCREEN_OFFSET;
      static const float                                  LOCKED_SLICE_DISTANCE_OFFSET;
      static const float                                  LOCKED_SLICE_SCALE_FACTOR;
      static const float                                  LERP_RATE;

    };
  }
}