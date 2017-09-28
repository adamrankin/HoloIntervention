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

// Local includes
#include "pch.h"
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "RenderingCommon.h"
#include "SliceEntry.h"
#include "SliceRenderer.h"
#include "StepTimer.h"

// DirectXTex includes
#include <DirectXTex.h>

// DirectXTK includes
#include <WICTextureLoader.h>

// Direct3D includes
#include <d3d11_4.h>

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    const float SliceEntry::LOCKED_SLICE_DISTANCE_OFFSET = 2.1f;
    const float SliceEntry::LERP_RATE = 2.5f;

    //----------------------------------------------------------------------------
    float3 SliceEntry::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      return float3(m_currentPose.m41, m_currentPose.m42, m_currentPose.m43);
    }

    //----------------------------------------------------------------------------
    float3 SliceEntry::GetStabilizedVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    float SliceEntry::GetStabilizePriority() const
    {
      // Priority is determined by systems that use this slice entry
      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    SliceEntry::SliceEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& timer)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
    {
      SetBlackMapColour(m_blackMapColour);
      SetWhiteMapColour(m_whiteMapColour);
    }

    //----------------------------------------------------------------------------
    SliceEntry::~SliceEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    bool SliceEntry::IsInFrustum() const
    {
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool SliceEntry::IsInFrustum(const SpatialBoundingFrustum& frustum) const
    {
      if (m_timer.GetFrameCount() == m_frustumCheckFrameNumber)
      {
        return m_isInFrustum;
      }

      const float bottom = -0.5;
      const float left = -0.5;
      const float right = 0.5;
      const float top = 0.5;

      const std::vector<float3> points =
      {
        transform(float3(left, top, 0.f), m_currentPose),
        transform(float3(right, top, 0.f), m_currentPose),
        transform(float3(right, bottom, 0.f), m_currentPose),
        transform(float3(left, bottom, 0.f), m_currentPose)
      };

      m_isInFrustum = HoloIntervention::IsInFrustum(frustum, points);
      m_frustumCheckFrameNumber = m_timer.GetFrameCount();
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Update(SpatialPointerPose^ pose)
    {
      if (!m_sliceValid)
      {
        return;
      }

      const float& deltaTime = static_cast<float>(m_timer.GetElapsedSeconds());

      float3 currentTranslation = { m_currentPose.m41, m_currentPose.m42, m_currentPose.m43 };
      float3 lastTranslation = { m_lastPose.m41, m_lastPose.m42, m_lastPose.m43 };

      const float3 deltaPosition = currentTranslation - lastTranslation; // meters
      m_velocity = deltaPosition * (1.f / deltaTime); // meters per second
      m_lastPose = m_currentPose;

      // Calculate new smoothed currentPose
      if (!m_headLocked)
      {
        if (m_firstFrame)
        {
          m_currentPose = m_desiredPose;
          m_firstFrame = false;
        }
        else
        {
          m_currentPose = lerp(m_currentPose, m_desiredPose, deltaTime * LERP_RATE);
        }
      }
      else
      {
        // Get the gaze direction relative to the given coordinate system.
        const float3 offsetFromGaze = pose->Head->Position + (float3(LOCKED_SLICE_DISTANCE_OFFSET) * pose->Head->ForwardDirection);

        // Use linear interpolation to smooth the position over time
        float3 smoothedPosition;
        if (m_firstFrame)
        {
          smoothedPosition = offsetFromGaze;
          m_firstFrame = false;
        }
        else
        {
          smoothedPosition = lerp(currentTranslation, offsetFromGaze, deltaTime * LERP_RATE);
        }

        float4x4 worldTransform;
        if (m_useHeadUpDirection)
        {
          worldTransform = make_float4x4_world(smoothedPosition, pose->Head->ForwardDirection, pose->Head->UpDirection);
        }
        else
        {
          worldTransform = make_float4x4_world(smoothedPosition, pose->Head->ForwardDirection, float3(0.f, 1.f, 0.f));
        }
        m_currentPose = make_float4x4_scale(m_scalingFactor.x, m_scalingFactor.y, 1.f) * worldTransform;
      }

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&m_currentPose));

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_sliceConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Render(uint32 indexCount)
    {
      if (!m_visible || !m_sliceValid)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPositionTexture);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);

      context->VSSetConstantBuffers(0, 1, m_sliceConstantBuffer.GetAddressOf());
      context->PSSetConstantBuffers(0, 1, m_sliceConstantBuffer.GetAddressOf());
      context->PSSetShaderResources(0, 1, m_shaderResourceView.GetAddressOf());

      context->DrawIndexedInstanced(indexCount, 2, 0, 0, 0);
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetFrame(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      std::shared_ptr<byte> image = *(std::shared_ptr<byte>*)(frame->GetImageData());
      if (image == nullptr)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, "Unable to access image buffer.");
        return;
      }

      auto frameSize = frame->Dimensions;
      auto format = (DXGI_FORMAT)frame->GetPixelFormat(true);
      if (frameSize[0] != m_width || frameSize[1] != m_height || format != GetPixelFormat())
      {
        m_width = frameSize[0];
        m_height = frameSize[1];
        m_pixelFormat = format;
        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources();
      }

      m_frame = frame;

      auto context = m_deviceResources->GetD3DDeviceContext();

      auto bytesPerPixel = BitsPerPixel(GetPixelFormat()) / 8;

      byte* imageRaw = image.get();
      D3D11_MAPPED_SUBRESOURCE mappedResource;
      context->Map(m_imageStagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mappedResource);
      byte* mappedData = reinterpret_cast<byte*>(mappedResource.pData);
      for (uint32 i = 0; i < m_height; ++i)
      {
        memcpy(mappedData, imageRaw, m_width * bytesPerPixel);
        mappedData += mappedResource.RowPitch;
        imageRaw += m_width * bytesPerPixel;
      }

      context->Unmap(m_imageStagingTexture.Get(), 0);

      context->CopyResource(m_imageTexture.Get(), m_imageStagingTexture.Get());
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat)
    {
      if (width != m_width || height != m_height || pixelFormat != m_pixelFormat)
      {
        m_width = width;
        m_height = height;
        m_pixelFormat = pixelFormat;
        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources();
      }

      m_imageData = imageData;

      auto context = m_deviceResources->GetD3DDeviceContext();

      auto bytesPerPixel = BitsPerPixel(pixelFormat) / 8;

      D3D11_MAPPED_SUBRESOURCE mappedResource;
      context->Map(m_imageStagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mappedResource);
      byte* image = m_imageData.get();
      byte* mappedData = reinterpret_cast<byte*>(mappedResource.pData);
      for (uint32 i = 0; i < m_height; ++i)
      {
        memcpy(mappedData, image, m_width * bytesPerPixel);
        mappedData += mappedResource.RowPitch;
        image += m_width * bytesPerPixel;
      }

      context->Unmap(m_imageStagingTexture.Get(), 0);

      context->CopyResource(m_imageTexture.Get(), m_imageStagingTexture.Get());
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetImageData(const std::wstring& fileName)
    {
      TexMetadata metadata;
      GetMetadataFromWICFile(fileName.c_str(), WIC_FLAGS_NONE, metadata);

      if (metadata.width != m_width || metadata.height != m_height || metadata.format != GetPixelFormat())
      {
        m_width = static_cast<uint16>(metadata.width);
        m_height = static_cast<uint16>(metadata.height);
        m_pixelFormat = metadata.format;
        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources();
      }

      m_imageTexture.Reset();
      m_shaderResourceView.Reset();
      m_imageData = nullptr;

      CreateWICTextureFromFile(m_deviceResources->GetD3DDevice(), m_deviceResources->GetD3DDeviceContext(), fileName.c_str(), (ID3D11Resource**)m_imageTexture.GetAddressOf(), nullptr);
      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_imageTexture.Get(), nullptr, &m_shaderResourceView));
#if _DEBUG
      m_shaderResourceView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strnlen_s("sliceEntrySRVFilename", MAX_PATH)), "sliceEntrySRVFilename");
#endif
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte> SliceEntry::GetImageData() const
    {
      return m_imageData;
    }

    //-----------------------------------------------------------------------------
    void SliceEntry::SetVertexBuffer(Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer)
    {
      m_vertexBuffer = vertexBuffer;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::ForceCurrentPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_firstFrame = true;
      m_currentPose = m_desiredPose = m_lastPose = matrix;
    }

    //----------------------------------------------------------------------------
    float4x4 SliceEntry::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    bool SliceEntry::GetVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetVisible(bool visible)
    {
      m_visible = visible;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetHeadlocked(bool headLocked)
    {
      m_headLocked = headLocked;
    }

    //----------------------------------------------------------------------------
    bool SliceEntry::GetHeadlocked() const
    {
      return m_headLocked;
    }

    //-----------------------------------------------------------------------------
    void SliceEntry::SetUseHeadUpDirection(bool use)
    {
      m_useHeadUpDirection = use;
    }

    //-----------------------------------------------------------------------------
    bool SliceEntry::GetUseHeadUpDirection() const
    {
      return m_useHeadUpDirection;
    }

    //-----------------------------------------------------------------------------
    void SliceEntry::SetScalingFactor(float x, float y)
    {
      m_scalingFactor = float2(x, y);
    }

    //-----------------------------------------------------------------------------
    void SliceEntry::SetScalingFactor(const float2& scale)
    {
      m_scalingFactor = scale;
    }

    //-----------------------------------------------------------------------------
    void SliceEntry::SetScalingFactor(float uniformScale)
    {
      m_scalingFactor = float2(uniformScale, uniformScale);
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    uint64 SliceEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetWhiteMapColour(float4 colour)
    {
      float4 blackMapColour(m_constantBuffer.blackMapColour.x, m_constantBuffer.blackMapColour.y, m_constantBuffer.blackMapColour.z, m_constantBuffer.blackMapColour.w);
      XMStoreFloat4(&m_constantBuffer.whiteMinusBlackColour, XMLoadFloat4(&(colour - blackMapColour)));
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetBlackMapColour(float4 colour)
    {
      XMStoreFloat4(&m_constantBuffer.blackMapColour, XMLoadFloat4(&colour));
      SetWhiteMapColour(m_whiteMapColour);
    }

    //----------------------------------------------------------------------------
    void SliceEntry::CreateDeviceDependentResources()
    {
      auto device = m_deviceResources->GetD3DDevice();

      const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(SliceConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&constantBufferDesc, nullptr, &m_sliceConstantBuffer));

      if (GetPixelFormat() != DXGI_FORMAT_UNKNOWN && m_width > 0 && m_height > 0)
      {
        CD3D11_TEXTURE2D_DESC textureDesc(GetPixelFormat(), m_width, m_height, 1, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
        DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageStagingTexture));

        textureDesc = CD3D11_TEXTURE2D_DESC(GetPixelFormat(), m_width, m_height, 1, 0, D3D11_BIND_SHADER_RESOURCE);
        DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageTexture));
        DX::ThrowIfFailed(device->CreateShaderResourceView(m_imageTexture.Get(), nullptr, &m_shaderResourceView));
#if _DEBUG
        m_shaderResourceView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strnlen_s("sliceEntrySRV", MAX_PATH)), "sliceEntrySRV");
#endif
      }

      m_sliceValid = true;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::ReleaseDeviceDependentResources()
    {
      m_sliceValid = false;
      m_sliceConstantBuffer.Reset();
      m_shaderResourceView.Reset();
      m_imageTexture.Reset();
      m_imageStagingTexture.Reset();
      m_vertexBuffer = nullptr;
    }

    //----------------------------------------------------------------------------
    DXGI_FORMAT SliceEntry::GetPixelFormat() const
    {
      return m_pixelFormat;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetPixelFormat(DXGI_FORMAT val)
    {
      m_pixelFormat = val;
    }
  }
}