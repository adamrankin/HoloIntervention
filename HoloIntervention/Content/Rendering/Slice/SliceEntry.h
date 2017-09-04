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
#include "IStabilizedComponent.h"

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    struct SliceConstantBuffer
    {
      DirectX::XMFLOAT4X4 worldMatrix;
      DirectX::XMFLOAT4   blackMapColour;
      DirectX::XMFLOAT4   whiteMinusBlackColour;
    };

    static_assert((sizeof(SliceConstantBuffer) % (sizeof(float) * 4)) == 0, "Slice constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    class SliceEntry : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      SliceEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& timer);
      ~SliceEntry();

      bool IsInFrustum() const;
      bool IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum) const;

      void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ pose);
      void Render(uint32 indexCount);

      void SetFrame(UWPOpenIGTLink::TrackedFrame^ frame);
      void SetImageData(const std::wstring& fileName);
      void SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat);
      std::shared_ptr<byte> GetImageData() const;

      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix);
      void ForceCurrentPose(const Windows::Foundation::Numerics::float4x4& matrix);
      Windows::Foundation::Numerics::float4x4 GetCurrentPose() const;

      bool GetVisible() const;
      void SetVisible(bool visible);

      void SetHeadlocked(bool headLocked);
      bool GetHeadlocked() const;

      void SetId(uint64 id);
      uint64 GetId() const;

      void SetWhiteMapColour(Windows::Foundation::Numerics::float4 colour);
      void SetBlackMapColour(Windows::Foundation::Numerics::float4 colour);

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      DXGI_FORMAT GetPixelFormat() const;
      void SetPixelFormat(DXGI_FORMAT val);

    protected:
      // Cached pointer
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;
      DX::StepTimer&                                      m_timer;

      // D3D resources
      Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_imageTexture;
      Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_imageStagingTexture;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_shaderResourceView;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_sliceConstantBuffer;

      // State vars
      uint64                                              m_id = 0;
      SliceConstantBuffer                                 m_constantBuffer;
      std::atomic_bool                                    m_sliceValid = false;
      std::atomic_bool                                    m_headLocked = false;
      std::atomic_bool                                    m_visible = true;
      Windows::Foundation::Numerics::float4x4             m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_currentPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float3               m_velocity = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float4               m_whiteMapColour = { 1.f, 1.f, 1.f, 1.f };
      Windows::Foundation::Numerics::float4               m_blackMapColour = { 0.f, 0.f, 0.f, 1.f };
      float                                               m_scalingFactor = 1.f;
      DXGI_FORMAT                                         m_pixelFormat = DXGI_FORMAT_UNKNOWN;
      mutable std::atomic_bool                            m_isInFrustum = false;
      mutable uint64                                      m_frustumCheckFrameNumber = 0;

      // Image data vars
      UWPOpenIGTLink::TrackedFrame^                       m_frame = nullptr;
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