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

// Local includes
#include "pch.h"
#include "SliceEntry.h"

// Common includes
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// DirectXTex includes
#include <DirectXTex.h>

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace
{
  // Function taken from https://github.com/mrdooz/kumi/blob/master/animation_manager.cpp
  float4x4 MatrixCompose(const float3& pos, const quaternion& rot, const float3& scale, bool transpose)
  {
    XMFLOAT3 zero = { 0, 0, 0 };
    XMFLOAT4 id = { 0, 0, 0, 1 };
    XMVECTOR vZero = XMLoadFloat3(&zero);
    XMVECTOR qId = XMLoadFloat4(&id);
    XMVECTOR qRot = XMLoadQuaternion(&rot);
    XMVECTOR vPos = XMLoadFloat3(&pos);
    XMVECTOR vScale = XMLoadFloat3(&scale);

    XMMATRIX mtx = XMMatrixTransformation(vZero, qId, vScale, vZero, qRot, vPos);
    if (transpose)
    {
      mtx = XMMatrixTranspose(mtx);
    }
    float4x4 res;
    XMStoreFloat4x4(&res, mtx);

    return res;
  }
}

namespace HoloIntervention
{
  namespace Rendering
  {
    const float3 SliceEntry::LOCKED_SLICE_SCREEN_OFFSET = { 0.12f, 0.f, 0.f };
    const float SliceEntry::LOCKED_SLICE_DISTANCE_OFFSET = 2.1f;
    const float SliceEntry::LOCKED_SLICE_SCALE_FACTOR = 10.f;
    const float SliceEntry::LERP_RATE = 2.5f;

    //----------------------------------------------------------------------------
    SliceEntry::SliceEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
    }

    //----------------------------------------------------------------------------
    SliceEntry::~SliceEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Update(SpatialPointerPose^ pose, const DX::StepTimer& timer)
    {
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      float3 currentScale;
      quaternion currentRotation;
      float3 currentTranslation;
      decompose(m_currentPose, &currentScale, &currentRotation, &currentTranslation);

      float3 lastScale;
      quaternion lastRotation;
      float3 lastTranslation;
      decompose(m_currentPose, &lastScale, &lastRotation, &lastTranslation);

      const float3 deltaPosition = currentTranslation - lastTranslation; // meters
      m_velocity = deltaPosition * (1.f / deltaTime); // meters per second
      m_lastPose = m_currentPose;

      // Calculate new smoothed currentPose
      if (!m_headLocked)
      {
        float3 desiredScale;
        quaternion desiredRotation;
        float3 desiredTranslation;
        decompose(m_desiredPose, &desiredScale, &desiredRotation, &desiredTranslation);

        float3 smoothedScale = lerp(currentScale, desiredScale, deltaTime * LERP_RATE);
        quaternion smoothedRotation = slerp(currentRotation, desiredRotation, deltaTime * LERP_RATE);
        float3 smoothedTranslation = lerp(currentTranslation, desiredTranslation, deltaTime * LERP_RATE);

        m_currentPose = MatrixCompose(smoothedTranslation, smoothedRotation, smoothedScale, true);
      }
      else
      {
        // Get the gaze direction relative to the given coordinate system.
        const float3 headPosition = pose->Head->Position;
        const float3 headDirection = pose->Head->ForwardDirection;

        // Offset the view to centered, lower quadrant
        const float3 offsetFromGaze = headPosition + (float3(LOCKED_SLICE_DISTANCE_OFFSET) * (headDirection + LOCKED_SLICE_SCREEN_OFFSET));

        // Use linear interpolation to smooth the position over time
        float3 f3_currentTranslation = { currentTranslation.x, currentTranslation.y, currentTranslation.z };
        const float3 smoothedPosition = lerp(f3_currentTranslation, offsetFromGaze, deltaTime * LERP_RATE);

        XMVECTOR facingNormal = XMVector3Normalize(-XMLoadFloat3(&smoothedPosition));
        XMVECTOR xAxisRotation = XMVector3Normalize(XMVectorSet(XMVectorGetZ(facingNormal), 0.f, -XMVectorGetX(facingNormal), 0.f));
        XMVECTOR yAxisRotation = XMVector3Normalize(XMVector3Cross(facingNormal, xAxisRotation));

        // Construct the 4x4 pose matrix.
        float4x4 rotationMatrix;
        XMStoreFloat4x4(&rotationMatrix, XMMATRIX(xAxisRotation, yAxisRotation, facingNormal, XMVectorSet(0.f, 0.f, 0.f, 1.f)));
        m_currentPose = make_float4x4_scale(m_scalingFactor, m_scalingFactor, 1.f) * rotationMatrix * make_float4x4_translation(smoothedPosition); // TODO : this order may need to be reversed due to column-major change
      }

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&m_currentPose));

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_sliceConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Render(uint32 indexCount)
    {
      if (!m_showing || m_imageData == nullptr)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      context->VSSetConstantBuffers(0, 1, m_sliceConstantBuffer.GetAddressOf());
      context->PSSetShaderResources(0, 1, m_shaderResourceView.GetAddressOf());

      context->DrawIndexedInstanced(indexCount, 2, 0, 0, 0);
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
    std::shared_ptr<byte> SliceEntry::GetImageData() const
    {
      return m_imageData;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    float3 SliceEntry::GetSliceVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetHeadlocked(bool headLocked)
    {
      m_headLocked = headLocked;
      if (m_headLocked)
      {
        m_scalingFactor = LOCKED_SLICE_SCALE_FACTOR;
      }
      else
      {
        m_scalingFactor = 1.f;
      }
    }

    //----------------------------------------------------------------------------
    void SliceEntry::CreateDeviceDependentResources()
    {
      auto device = m_deviceResources->GetD3DDevice();

      const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(SliceConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&constantBufferDesc, nullptr, &m_sliceConstantBuffer));

      if (m_pixelFormat != DXGI_FORMAT_UNKNOWN && m_width > 0 && m_height > 0)
      {
        CD3D11_TEXTURE2D_DESC textureDesc(m_pixelFormat, m_width, m_height, 1, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
        DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageStagingTexture));

        textureDesc = CD3D11_TEXTURE2D_DESC(m_pixelFormat, m_width, m_height, 1, 0, D3D11_BIND_SHADER_RESOURCE);
        DX::ThrowIfFailed(device->CreateTexture2D(&textureDesc, nullptr, &m_imageTexture));
        DX::ThrowIfFailed(device->CreateShaderResourceView(m_imageTexture.Get(), nullptr, &m_shaderResourceView));
      }
    }

    //----------------------------------------------------------------------------
    void SliceEntry::ReleaseDeviceDependentResources()
    {
      m_sliceConstantBuffer.Reset();
      m_shaderResourceView.Reset();
      m_imageTexture.Reset();
      m_imageStagingTexture.Reset();
    }
  }
}