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
#include "AppView.h"
#include "Volume.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// DirectXTex includes
#include <DirectXTex.h>

// DirectX includes
#include <d3d11_3.h>
#include <DirectXColors.h>

// Unnecessary, but reduces intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
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
    const float Volume::LERP_RATE = 2.5f;

    //----------------------------------------------------------------------------
    Volume::Volume(const std::shared_ptr<DX::DeviceResources>& deviceResources, uint64 token, ID3D11Buffer* cwIndexBuffer, ID3D11Buffer* ccwIndexBuffer, ID3D11InputLayout* inputLayout, ID3D11Buffer* vertexBuffer, ID3D11VertexShader* volRenderVertexShader, ID3D11GeometryShader* volRenderGeometryShader, ID3D11PixelShader* volRenderPixelShader, ID3D11PixelShader* faceCalcPixelShader, ID3D11Texture2D* frontPositionTextureArray, ID3D11Texture2D* backPositionTextureArray, ID3D11RenderTargetView* frontPositionRTV, ID3D11RenderTargetView* backPositionRTV, ID3D11ShaderResourceView* frontPositionSRV, ID3D11ShaderResourceView* backPositionSRV, DX::StepTimer& timer)
      : m_deviceResources(deviceResources)
      , m_token(token)
      , m_cwIndexBuffer(cwIndexBuffer)
      , m_ccwIndexBuffer(ccwIndexBuffer)
      , m_inputLayout(inputLayout)
      , m_vertexBuffer(vertexBuffer)
      , m_volRenderVertexShader(volRenderVertexShader)
      , m_volRenderGeometryShader(volRenderGeometryShader)
      , m_volRenderPixelShader(volRenderPixelShader)
      , m_faceCalcPixelShader(faceCalcPixelShader)
      , m_frontPositionTextureArray(frontPositionTextureArray)
      , m_backPositionTextureArray(backPositionTextureArray)
      , m_frontPositionRTV(frontPositionRTV)
      , m_backPositionRTV(backPositionRTV)
      , m_frontPositionSRV(frontPositionSRV)
      , m_backPositionSRV(backPositionSRV)
      , m_timer(timer)
    {
      ControlPointList points;
      points.push_back(ControlPoint(0.f, float4(0.f, 0.f, 0.f, 0.f)));
      points.push_back(ControlPoint(255.f, float4(0.f, 0.f, 0.f, 1.f)));
      SetOpacityTransferFunctionTypeAsync(TransferFunction_Piecewise_Linear, 512, points);

      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    Volume::~Volume()
    {
      ReleaseDeviceDependentResources();
      delete m_opacityTransferFunction;
    }

    //----------------------------------------------------------------------------
    bool Volume::IsInFrustum() const
    {
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool Volume::IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum) const
    {
      if (m_timer.GetFrameCount() == m_frustumCheckFrameNumber)
      {
        return m_isInFrustum;
      }

      const std::vector<float3> points =
      {
        transform(float3(0.f, 0.f, 0.f), m_currentPose),
        transform(float3(0.f, 0.f, 1.f), m_currentPose),
        transform(float3(0.f, 1.f, 0.f), m_currentPose),
        transform(float3(0.f, 1.f, 1.f), m_currentPose),
        transform(float3(1.f, 0.f, 0.f), m_currentPose),
        transform(float3(1.f, 0.f, 1.f), m_currentPose),
        transform(float3(1.f, 1.f, 0.f), m_currentPose),
        transform(float3(1.f, 1.f, 1.f), m_currentPose)
      };

      m_isInFrustum = HoloIntervention::IsInFrustum(frustum, points);
      m_frustumCheckFrameNumber = m_timer.GetFrameCount();
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool Volume::IsValid() const
    {
      return m_volumeReady;
    }

    //----------------------------------------------------------------------------
    void Volume::Update()
    {
      if (!m_tfResourcesReady)
      {
        // nothing to do!
        return;
      }

      auto context = m_deviceResources->GetD3DDeviceContext();
      auto device = m_deviceResources->GetD3DDevice();

      const float& deltaTime = static_cast<float>(m_timer.GetElapsedSeconds());

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
      float3 desiredScale;
      quaternion desiredRotation;
      float3 desiredTranslation;
      decompose(m_desiredPose, &desiredScale, &desiredRotation, &desiredTranslation);

      float3 smoothedScale = lerp(currentScale, desiredScale, deltaTime * LERP_RATE);
      quaternion smoothedRotation = slerp(currentRotation, desiredRotation, deltaTime * LERP_RATE);
      float3 smoothedTranslation = lerp(currentTranslation, desiredTranslation, deltaTime * LERP_RATE);

      m_currentPose = MatrixCompose(smoothedTranslation, smoothedRotation, smoothedScale, true);

      if (m_volumeUpdateNeeded)
      {
        ReleaseVolumeResources();
        CreateVolumeResources();
        m_volumeUpdateNeeded = false;
      }

      if (m_onGPUFrame != m_frame)
      {
        UpdateGPUImageData();
      }

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&m_currentPose));
      context->UpdateSubresource(m_volumeEntryConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void Volume::Render(uint32 indexCount)
    {
      if (!m_volumeReady || !m_tfResourcesReady)
      {
        return;
      }

      ID3D11DeviceContext3* context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPosition);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, &m_vertexBuffer, &stride, &offset);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout);

      // Cache render target
      ID3D11RenderTargetView* hololensRenderTargetView;
      ID3D11DepthStencilView* hololensStencilView;
      context->OMGetRenderTargets(1, &hololensRenderTargetView, &hololensStencilView);

      context->ClearRenderTargetView(m_frontPositionRTV, DirectX::Colors::Black);
      context->ClearRenderTargetView(m_backPositionRTV, DirectX::Colors::Black);

      context->RSSetState(nullptr);

      // Set index buffer to cw winding to calculate front faces
      context->IASetIndexBuffer(m_cwIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
      ID3D11RenderTargetView* targets[1] = { m_frontPositionRTV };
      context->OMSetRenderTargets(1, targets, nullptr);
      context->VSSetShader(m_volRenderVertexShader, nullptr, 0);
      context->VSSetConstantBuffers(0, 1, m_volumeEntryConstantBuffer.GetAddressOf());
      if (!m_deviceResources->GetDeviceSupportsVprt())
      {
        context->GSSetShader(m_volRenderGeometryShader, nullptr, 0);
        context->GSSetConstantBuffers(0, 1, m_volumeEntryConstantBuffer.GetAddressOf());
      }
      context->PSSetShader(m_faceCalcPixelShader, nullptr, 0);
      context->DrawIndexedInstanced(indexCount, 2, 0, 0, 0);

      // Set index buffer to ccw winding to calculate back faces
      context->IASetIndexBuffer(m_ccwIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
      targets[0] = m_backPositionRTV;
      context->OMSetRenderTargets(1, targets, nullptr);
      context->DrawIndexedInstanced(indexCount, 2, 0, 0, 0);

      // Now perform the actual volume render
      targets[0] = hololensRenderTargetView;
      context->OMSetRenderTargets(1, targets, hololensStencilView);
      context->IASetIndexBuffer(m_cwIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
      ID3D11ShaderResourceView* shaderResourceViews[4] = { m_opacityLookupTableSRV.Get(), m_volumeSRV.Get(), m_frontPositionSRV, m_backPositionSRV };
      context->PSSetShaderResources(0, 4, shaderResourceViews);
      ID3D11SamplerState* samplerStates[1] = { m_samplerState.Get() };
      context->PSSetSamplers(0, 1, samplerStates);
      context->PSSetConstantBuffers(0, 1, m_volumeEntryConstantBuffer.GetAddressOf());
      context->PSSetShader(m_volRenderPixelShader, nullptr, 0);
      context->DrawIndexedInstanced(indexCount, 2, 0, 0, 0);

      // Clear values
      ID3D11ShaderResourceView* ppSRVnullptr[4] = { nullptr, nullptr, nullptr, nullptr };
      context->PSSetShaderResources(0, 4, ppSRVnullptr);
      ID3D11SamplerState* ppSamplerStatesnullptr[1] = { nullptr };
      context->PSSetSamplers(0, 1, ppSamplerStatesnullptr);
    }

    //----------------------------------------------------------------------------
    void Volume::SetFrame(UWPOpenIGTLink::VideoFrame^ frame)
    {
      auto frameSize = frame->Dimensions;
      if (frameSize[2] < 1)
      {
        return;
      }

      if (!m_volumeReady)
      {
        m_volumeUpdateNeeded = true;
      }
      else if (m_frame != nullptr)
      {
        if (m_frame != nullptr)
        {
          auto myFrameSize = m_frame->Dimensions;
          if (myFrameSize[0] != frameSize[0] || myFrameSize[1] != frameSize[1] || myFrameSize[2] != frameSize[2])
          {
            // GPU needs to be reallocated
            m_volumeUpdateNeeded = true;
          }
        }
      }

      m_frame = frame;
    }

    //----------------------------------------------------------------------------
    void Volume::SetShowing(bool showing)
    {
      m_showing = showing;
    }

    //----------------------------------------------------------------------------
    uint64 Volume::GetToken() const
    {
      return m_token;
    }

    //----------------------------------------------------------------------------
    void Volume::ForceCurrentPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = m_currentPose = matrix;
    }

    //----------------------------------------------------------------------------
    void Volume::SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    float4x4 Volume::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    float3 Volume::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void Volume::UpdateGPUImageData()
    {
      const auto context = m_deviceResources->GetD3DDeviceContext();

      auto bytesPerPixel = BitsPerPixel((DXGI_FORMAT)m_frame->GetPixelFormat(true)) / 8;

      std::shared_ptr<byte> image = *(std::shared_ptr<byte>*)(m_frame->Image->GetImageData());
      if (image == nullptr)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, "Unable to access image buffer.");
        return;
      }

      auto frameSize = m_frame->Dimensions;
      if (frameSize[2] < 1)
      {
        return;
      }

      // Map image resource and update data
      byte* imageRaw = image.get();
      D3D11_MAPPED_SUBRESOURCE mappedResource;
      context->Map(m_volumeStagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mappedResource);
      byte* mappedData = reinterpret_cast<byte*>(mappedResource.pData);
      for (uint32 j = 0; j < frameSize[2]; ++j)
      {
        for (uint32 i = 0; i < frameSize[1]; ++i)
        {
          memcpy(mappedData, imageRaw, frameSize[0] * bytesPerPixel);
          mappedData += mappedResource.RowPitch;
          imageRaw += frameSize[0] * bytesPerPixel;
        }
      }
      context->Unmap(m_volumeStagingTexture.Get(), 0);

      context->CopyResource(m_volumeTexture.Get(), m_volumeStagingTexture.Get());

      m_onGPUFrame = m_frame;
    }

    //----------------------------------------------------------------------------
    void Volume::CreateDeviceDependentResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_opacityTFType != TransferFunction_Unknown)
      {
        std::lock_guard<std::mutex> guard(m_opacityTFMutex);
        CreateTFResources();
      }

      if (m_frame != nullptr)
      {
        CreateVolumeResources();
      }

      VolumeEntryConstantBuffer buffer;
      XMStoreFloat4x4(&buffer.worldMatrix, XMMatrixIdentity());
      D3D11_SUBRESOURCE_DATA resData;
      resData.pSysMem = &buffer;
      resData.SysMemPitch = 0;
      resData.SysMemSlicePitch = 0;

      DX::ThrowIfFailed(device->CreateBuffer(&CD3D11_BUFFER_DESC(sizeof(VolumeEntryConstantBuffer), D3D11_BIND_CONSTANT_BUFFER), &resData, m_volumeEntryConstantBuffer.GetAddressOf()));
    }

    //----------------------------------------------------------------------------
    void Volume::ReleaseDeviceDependentResources()
    {
      ReleaseVolumeResources();
      ReleaseTFResources();

      m_volumeEntryConstantBuffer.Reset();
    }

    //----------------------------------------------------------------------------
    void Volume::CreateVolumeResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_frame == nullptr)
      {
        return;
      }

      auto format = (DXGI_FORMAT)m_frame->GetPixelFormat(true);
      auto bytesPerPixel = BitsPerPixel(format) / 8;
      byte* imageRaw = GetDataFromIBuffer<byte>(m_frame->Image->ImageData);
      if (imageRaw == nullptr)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, "Unable to access image buffer.");
        return;
      }

      auto frameSize = m_frame->Dimensions;
      if (frameSize[2] < 1)
      {
        return;
      }

      // Create a staging texture that will be used to copy data from the CPU to the GPU,
      // the staging texture will then copy to the render texture
      CD3D11_TEXTURE3D_DESC textureDesc(format, frameSize[0], frameSize[1], frameSize[2], 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
      D3D11_SUBRESOURCE_DATA imgData;
      imgData.pSysMem = imageRaw;
      imgData.SysMemPitch = frameSize[0] * bytesPerPixel;
      imgData.SysMemSlicePitch = frameSize[0] * frameSize[1] * bytesPerPixel;
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeStagingTexture.GetAddressOf()));

      // Create the texture that will be used by the shader to access the current volume to be rendered
      textureDesc = CD3D11_TEXTURE3D_DESC(format, frameSize[0], frameSize[1], frameSize[2], 1);
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeTexture.GetAddressOf()));
#if _DEBUG
      m_volumeTexture->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolumeTexture") - 1, "VolumeTexture");
#endif
      CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(m_volumeTexture.Get(), format);
      DX::ThrowIfFailed(device->CreateShaderResourceView(m_volumeTexture.Get(), &srvDesc, m_volumeSRV.GetAddressOf()));
#if _DEBUG
      m_volumeSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolumeSRV") - 1, "VolumeSRV");
#endif

      // Compute the step size and number of iterations to use
      //    The step size for each component needs to be a ratio of the largest component
      float maxSize = std::fmaxf(frameSize[0], std::fmaxf(frameSize[1], frameSize[2]));
      float3 stepSize = float3(1.0f / (frameSize[0] * (maxSize / frameSize[0])),
                               1.0f / (frameSize[1] * (maxSize / frameSize[1])),
                               1.0f / (frameSize[2] * (maxSize / frameSize[2])));

      XMStoreFloat3(&m_constantBuffer.stepSize, XMLoadFloat3(&(stepSize * m_stepScale)));
      m_constantBuffer.numIterations = static_cast<uint32>(maxSize * (1.0f / m_stepScale));

      float borderColour[4] = { 0.f, 0.f, 0.f, 0.f };
      CD3D11_SAMPLER_DESC desc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER, 0.f, 3, D3D11_COMPARISON_NEVER, borderColour, 0, 3);
      DX::ThrowIfFailed(device->CreateSamplerState(&desc, m_samplerState.GetAddressOf()));
#if _DEBUG
      m_samplerState->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendSamplerState") - 1, "VolRendSamplerState");
#endif

      m_volumeReady = true;
    }

    //----------------------------------------------------------------------------
    void Volume::ReleaseVolumeResources()
    {
      m_volumeReady = false;
      m_volumeStagingTexture.Reset();
      m_volumeTexture.Reset();
      m_volumeSRV.Reset();
      m_samplerState.Reset();
    }

    //----------------------------------------------------------------------------
    task<void> Volume::SetOpacityTransferFunctionTypeAsync(TransferFunctionType functionType, uint32 tableSize, const ControlPointList& controlPoints)
    {
      return create_task([this, functionType, tableSize, controlPoints]()
      {
        std::lock_guard<std::mutex> guard(m_opacityTFMutex);

        delete m_opacityTransferFunction;
        switch (functionType)
        {
        case Volume::TransferFunction_Piecewise_Linear:
        {
          m_opacityTFType = Volume::TransferFunction_Piecewise_Linear;
          m_opacityTransferFunction = new PiecewiseLinearTransferFunction();
          break;
        }
        default:
          throw std::invalid_argument("Function type not recognized.");
          break;
        }

        for (auto& point : controlPoints)
        {
          m_opacityTransferFunction->AddControlPoint(point.first, point.second.w);
        }
        m_opacityTransferFunction->SetLookupTableSize(tableSize);
        m_opacityTransferFunction->Update();
      }).then([this]()
      {
        std::lock_guard<std::mutex> guard(m_opacityTFMutex);
        ReleaseTFResources();
        CreateTFResources();
      });
    }

    //----------------------------------------------------------------------------
    void Volume::CreateTFResources()
    {
      if (m_opacityTransferFunction == nullptr)
      {
        return;
      }

      if (!m_opacityTransferFunction->IsValid())
      {
        throw std::exception("Transfer function table not valid.");
      }

      m_opacityTransferFunction->Update();
      m_constantBuffer.lt_maximumXValue = m_opacityTransferFunction->GetMaximumXValue();
      m_constantBuffer.lt_arraySize = m_opacityTransferFunction->GetTFLookupTable().GetArraySize();

      // Set up GPU memory
      D3D11_BUFFER_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = sizeof(DirectX::XMFLOAT4) * m_opacityTransferFunction->GetTFLookupTable().GetArraySize();
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = sizeof(DirectX::XMFLOAT4);

      D3D11_SUBRESOURCE_DATA bufferBytes = { m_opacityTransferFunction->GetTFLookupTable().GetLookupTableArray(), 0, 0 };
      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&desc, &bufferBytes, m_opacityLookupTableBuffer.GetAddressOf()));
#if _DEBUG
      m_opacityLookupTableBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("OpacityLookupTable") - 1, "OpacityLookupTable");
#endif

      D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
      ZeroMemory(&srvDesc, sizeof(srvDesc));
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      srvDesc.BufferEx.FirstElement = 0;
      srvDesc.Format = DXGI_FORMAT_UNKNOWN;
      srvDesc.BufferEx.NumElements = m_opacityTransferFunction->GetTFLookupTable().GetArraySize();

      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_opacityLookupTableBuffer.Get(), &srvDesc, m_opacityLookupTableSRV.GetAddressOf()));
#if _DEBUG
      m_opacityLookupTableSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("OpacityLookupTableSRV") - 1, "OpacityLookupTableSRV");
#endif

      m_tfResourcesReady = true;
    }

    //----------------------------------------------------------------------------
    void Volume::ReleaseTFResources()
    {
      m_tfResourcesReady = false;
      m_opacityLookupTableSRV.Reset();
      m_opacityLookupTableBuffer.Reset();
    }
  }
}