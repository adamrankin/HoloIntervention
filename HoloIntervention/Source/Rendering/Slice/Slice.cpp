/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Slice.h"
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
    const float Slice::LOCKED_SLICE_DISTANCE_OFFSET = 2.1f;
    const float Slice::LERP_RATE = 2.5f;

    //----------------------------------------------------------------------------
    float3 Slice::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      return float3(m_currentPose.m41, m_currentPose.m42, m_currentPose.m43);
    }

    //----------------------------------------------------------------------------
    float3 Slice::GetStabilizedVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    float Slice::GetStabilizePriority() const
    {
      // Priority is determined by systems that use this slice entry
      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    Slice::Slice(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& timer, Debug& debug)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
      , m_debug(debug)
    {
      SetBlackMapColour(m_blackMapColour);
      SetWhiteMapColour(m_whiteMapColour);
    }

    //----------------------------------------------------------------------------
    Slice::~Slice()
    {
      ReleaseDeviceDependentResources();

      m_vertexBuffer = nullptr;
    }

    //----------------------------------------------------------------------------
    bool Slice::IsInFrustum() const
    {
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool Slice::IsInFrustum(const SpatialBoundingFrustum& frustum) const
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
    void Slice::Update(SpatialPointerPose^ pose)
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

        float4x4 worldTransform;
        if (m_useHeadUpDirection)
        {
          worldTransform = make_float4x4_world(offsetFromGaze, pose->Head->ForwardDirection, pose->Head->UpDirection);
        }
        else
        {
          worldTransform = make_float4x4_world(offsetFromGaze, pose->Head->ForwardDirection, float3(0.f, 1.f, 0.f));
        }

        if (m_firstFrame)
        {
          m_currentPose = make_float4x4_scale(m_scalingFactor.x, m_scalingFactor.y, 1.f) * worldTransform;
          m_firstFrame = false;
        }
        else
        {
          m_desiredPose = make_float4x4_scale(m_scalingFactor.x, m_scalingFactor.y, 1.f) * worldTransform;
          m_currentPose = lerp(m_currentPose, m_desiredPose, deltaTime * LERP_RATE);
        }
      }

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&m_currentPose));

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_sliceConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void Slice::Render(uint32 indexCount)
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

      ID3D11ShaderResourceView* ppNullptr[1] = { nullptr };
      context->PSSetShaderResources(0, 1, ppNullptr);
    }

    //----------------------------------------------------------------------------
    void Slice::SetFrame(UWPOpenIGTLink::VideoFrame^ frame)
    {
      std::shared_ptr<byte> image = *(std::shared_ptr<byte>*)(frame->GetImage()->GetImageData());
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
    void Slice::SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat)
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
    void Slice::SetImageData(const std::wstring& fileName)
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
    void Slice::SetImageData(Microsoft::WRL::ComPtr<ID3D11Texture2D> imageTexture)
    {
      if (imageTexture == nullptr)
      {
        return;
      }

      ReleaseDeviceDependentResources();

      m_ownTexture = false;
      m_imageData = nullptr;
      m_imageTexture = imageTexture;
      m_imageStagingTexture = nullptr;

      D3D11_TEXTURE2D_DESC desc;
      imageTexture->GetDesc(&desc);

      m_width = desc.Width;
      m_height = desc.Height;
      m_pixelFormat = desc.Format;

      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte> Slice::GetImageData() const
    {
      return m_imageData;
    }

    //-----------------------------------------------------------------------------
    void Slice::SetVertexBuffer(Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer)
    {
      m_vertexBuffer = vertexBuffer;
    }

    //----------------------------------------------------------------------------
    void Slice::SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    void Slice::ForceCurrentPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_firstFrame = true;
      m_currentPose = m_desiredPose = m_lastPose = matrix;
    }

    //----------------------------------------------------------------------------
    float4x4 Slice::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    bool Slice::GetVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    void Slice::SetVisible(bool visible)
    {
      m_visible = visible;
    }

    //----------------------------------------------------------------------------
    void Slice::SetHeadlocked(bool headLocked, bool smooth)
    {
      m_headLocked = headLocked;
      if (!smooth)
      {
        m_firstFrame = true;
      }
    }

    //----------------------------------------------------------------------------
    bool Slice::GetHeadlocked() const
    {
      return m_headLocked;
    }

    //-----------------------------------------------------------------------------
    void Slice::SetUseHeadUpDirection(bool use)
    {
      m_useHeadUpDirection = use;
    }

    //-----------------------------------------------------------------------------
    bool Slice::GetUseHeadUpDirection() const
    {
      return m_useHeadUpDirection;
    }

    //-----------------------------------------------------------------------------
    void Slice::SetScalingFactor(float x, float y)
    {
      m_scalingFactor = float2(x, y);
    }

    //-----------------------------------------------------------------------------
    void Slice::SetScalingFactor(const float2& scale)
    {
      m_scalingFactor = scale;
    }

    //-----------------------------------------------------------------------------
    void Slice::SetScalingFactor(float uniformScale)
    {
      m_scalingFactor = float2(uniformScale, uniformScale);
    }

    //----------------------------------------------------------------------------
    void Slice::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    uint64 Slice::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    bool Slice::IsValid() const
    {
      return m_sliceValid;
    }

    //----------------------------------------------------------------------------
    void Slice::SetColorizeGreyscale(bool colorize)
    {
      m_colorizeGreyscale = colorize;
    }

    //----------------------------------------------------------------------------
    bool Slice::GetColorizeGreyscale()
    {
      return m_colorizeGreyscale;
    }

    //----------------------------------------------------------------------------
    void Slice::SetWhiteMapColour(float4 colour)
    {
      m_whiteMapColour = colour;
      float4 blackMapColour(m_constantBuffer.blackMapColour.x, m_constantBuffer.blackMapColour.y, m_constantBuffer.blackMapColour.z, m_constantBuffer.blackMapColour.w);
      XMStoreFloat4(&m_constantBuffer.whiteMinusBlackColour, XMLoadFloat4(&(m_whiteMapColour - blackMapColour)));
    }

    //----------------------------------------------------------------------------
    void Slice::SetBlackMapColour(float4 colour)
    {
      XMStoreFloat4(&m_constantBuffer.blackMapColour, XMLoadFloat4(&colour));
      SetWhiteMapColour(m_whiteMapColour);
    }

    //----------------------------------------------------------------------------
    void Slice::CreateDeviceDependentResources()
    {
      auto device = m_deviceResources->GetD3DDevice();

      const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(SliceConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&constantBufferDesc, nullptr, &m_sliceConstantBuffer));

      if (GetPixelFormat() != DXGI_FORMAT_UNKNOWN && m_width > 0 && m_height > 0)
      {
        if (m_ownTexture)
        {
          CD3D11_TEXTURE2D_DESC textureDesc(GetPixelFormat(), m_width, m_height, 1, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
          DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageStagingTexture));

          textureDesc = CD3D11_TEXTURE2D_DESC(GetPixelFormat(), m_width, m_height, 1, 0, D3D11_BIND_SHADER_RESOURCE);
          DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageTexture));
        }
        DX::ThrowIfFailed(device->CreateShaderResourceView(m_imageTexture.Get(), nullptr, &m_shaderResourceView));
#if _DEBUG
        m_shaderResourceView->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strnlen_s("sliceEntrySRV", MAX_PATH)), "sliceEntrySRV");
#endif
      }

      m_sliceValid = true;
    }

    //----------------------------------------------------------------------------
    void Slice::ReleaseDeviceDependentResources()
    {
      m_sliceValid = false;
      m_sliceConstantBuffer.Reset();
      m_shaderResourceView.Reset();
      m_imageTexture.Reset();
      m_imageStagingTexture.Reset();
    }

    //----------------------------------------------------------------------------
    DXGI_FORMAT Slice::GetPixelFormat() const
    {
      return m_pixelFormat;
    }

    //----------------------------------------------------------------------------
    void Slice::SetPixelFormat(DXGI_FORMAT val)
    {
      m_pixelFormat = val;
    }
  }
}