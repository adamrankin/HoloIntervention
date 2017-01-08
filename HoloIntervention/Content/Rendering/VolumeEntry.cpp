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
#include "AppView.h"
#include "VolumeEntry.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// Network includes
#include "IGTLinkIF.h"

// System includes
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// DirectXTex includes
#include <DirectXTex.h>

// DirectX includes
#include <d3d11_3.h>
#include <DirectXColors.h>

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
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
    const float VolumeEntry::LERP_RATE = 2.5f;

    //----------------------------------------------------------------------------
    VolumeEntry::VolumeEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, uint64 token, ID3D11Buffer* cwIndexBuffer, ID3D11Buffer* ccwIndexBuffer, ID3D11InputLayout* inputLayout, ID3D11Buffer* vertexBuffer, ID3D11VertexShader* volRenderVertexShader, ID3D11GeometryShader* volRenderGeometryShader, ID3D11PixelShader* volRenderPixelShader, ID3D11PixelShader* faceCalcPixelShader, ID3D11Texture2D* frontPositionTextureArray, ID3D11Texture2D* backPositionTextureArray, ID3D11RenderTargetView* frontPositionRTV, ID3D11RenderTargetView* backPositionRTV, ID3D11ShaderResourceView* frontPositionSRV, ID3D11ShaderResourceView* backPositionSRV)
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
    {
      try
      {
        InitializeTransformRepositoryAsync(m_transformRepository, L"Assets\\Data\\configuration.xml");
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(e->Message);
      }

      ControlPointList points;
      points.push_back(ControlPoint(0.f, float4(0.f, 0.f, 0.f, 0.f)));
      points.push_back(ControlPoint(255.f, float4(0.f, 0.f, 0.f, 1.f)));
      SetOpacityTransferFunctionTypeAsync(TransferFunction_Piecewise_Linear, 512, points);

      try
      {
        GetXmlDocumentFromFileAsync(L"Assets\\Data\\configuration.xml").then([this](XmlDocument ^ doc)
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/VolumeRendering");
          if (doc->SelectNodes(xpath)->Length != 1)
          {
            // No configuration found, use defaults
            return;
          }

          IXmlNode^ volRendering = doc->SelectNodes(xpath)->Item(0);
          Platform::String^ fromAttribute = dynamic_cast<Platform::String^>(volRendering->Attributes->GetNamedItem(L"From")->NodeValue);
          Platform::String^ toAttribute = dynamic_cast<Platform::String^>(volRendering->Attributes->GetNamedItem(L"To")->NodeValue);
          if (fromAttribute->IsEmpty() || toAttribute->IsEmpty())
          {
            return;
          }
          else
          {
            m_fromCoordFrame = std::wstring(fromAttribute->Data());
            m_toCoordFrame = std::wstring(toAttribute->Data());
            m_imageToHMDName = ref new UWPOpenIGTLink::TransformName(fromAttribute, toAttribute);
          }
        });
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(e->Message);
      }

      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    VolumeEntry::~VolumeEntry()
    {
      ReleaseDeviceDependentResources();
      delete m_opacityTransferFunction;
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::Update(const DX::StepTimer& timer, DX::CameraResources* cameraResources, SpatialCoordinateSystem^ hmdCoordinateSystem, SpatialPointerPose^ headPose)
    {
      if (!m_tfResourcesReady)
      {
        // nothing to do!
        return;
      }

      auto context = m_deviceResources->GetD3DDeviceContext();
      auto device = m_deviceResources->GetD3DDevice();

      // Retrieve the current registration from reference to HMD
      float4x4 trackerToHMD = HoloIntervention::instance()->GetRegistrationSystem().GetTrackerToCoordinateSystemTransformation(hmdCoordinateSystem);
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(trackerToHMD), true);
      bool isValid;
      float4x4 transform = transpose(m_transformRepository->GetTransform(m_imageToHMDName, &isValid));
      if (!isValid)
      {
        return;
      }

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

      if (m_onGPUImageData != m_imageData)
      {
        UpdateGPUImageData();
      }

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&m_currentPose));
      context->UpdateSubresource(m_volumeEntryConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::Render(uint32 indexCount)
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
    void VolumeEntry::SetTransforms(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      if (m_transformRepository != nullptr)
      {
        m_transformRepository->SetTransforms(frame);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::SetImageData(std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat)
    {
      if (depth < 2)
      {
        return;
      }

      m_frameSize[0] = width;
      m_frameSize[1] = height;
      m_frameSize[2] = depth;
      m_pixelFormat = pixelFormat;
      m_imageData = imageData;

      if (!m_volumeReady)
      {
        m_volumeUpdateNeeded = true;
      }
      else if (width != m_frameSize[0] || height != m_frameSize[1] || depth != m_frameSize[2])
      {
        m_volumeUpdateNeeded = true;
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte> VolumeEntry::GetImageData() const
    {
      return m_imageData;
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::SetShowing(bool showing)
    {
      m_showing = showing;
    }

    //----------------------------------------------------------------------------
    uint64 VolumeEntry::GetToken() const
    {
      return m_token;
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::SetDesiredPose(const Windows::Foundation::Numerics::float4x4& matrix)
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    float3 VolumeEntry::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::UpdateGPUImageData()
    {
      const auto context = m_deviceResources->GetD3DDeviceContext();

      auto bytesPerPixel = BitsPerPixel(m_pixelFormat) / 8;

      // Map image resource and update data
      D3D11_MAPPED_SUBRESOURCE mappedResource;
      context->Map(m_volumeStagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mappedResource);
      byte* imageData = m_imageData.get();
      byte* mappedData = reinterpret_cast<byte*>(mappedResource.pData);
      for (uint32 j = 0; j < m_frameSize[2]; ++j)
      {
        for (uint32 i = 0; i < m_frameSize[1]; ++i)
        {
          memcpy(mappedData, imageData, m_frameSize[0] * bytesPerPixel);
          mappedData += mappedResource.RowPitch;
          imageData += m_frameSize[0] * bytesPerPixel;
        }
      }
      context->Unmap(m_volumeStagingTexture.Get(), 0);

      context->CopyResource(m_volumeTexture.Get(), m_volumeStagingTexture.Get());

      m_onGPUImageData = m_imageData;
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::CreateDeviceDependentResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_opacityTFType != TransferFunction_Unknown)
      {
        std::lock_guard<std::mutex> guard(m_opacityTFMutex);
        CreateTFResources();
      }

      if (m_imageData != nullptr)
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
    void VolumeEntry::ReleaseDeviceDependentResources()
    {
      ReleaseVolumeResources();
      ReleaseTFResources();

      m_volumeEntryConstantBuffer.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeEntry::CreateVolumeResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_imageData == nullptr)
      {
        return;
      }

      auto bytesPerPixel = BitsPerPixel(m_pixelFormat) / 8;

      // Create a staging texture that will be used to copy data from the CPU to the GPU,
      // the staging texture will then copy to the render texture
      CD3D11_TEXTURE3D_DESC textureDesc(m_pixelFormat, m_frameSize[0], m_frameSize[1], m_frameSize[2], 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
      D3D11_SUBRESOURCE_DATA imgData;
      imgData.pSysMem = m_imageData.get();
      imgData.SysMemPitch = m_frameSize[0] * bytesPerPixel;
      imgData.SysMemSlicePitch = m_frameSize[0] * m_frameSize[1] * bytesPerPixel;
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeStagingTexture.GetAddressOf()));

      // Create the texture that will be used by the shader to access the current volume to be rendered
      textureDesc = CD3D11_TEXTURE3D_DESC(m_pixelFormat, m_frameSize[0], m_frameSize[1], m_frameSize[2], 1);
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeTexture.GetAddressOf()));
#if _DEBUG
      m_volumeTexture->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolumeTexture") - 1, "VolumeTexture");
#endif
      CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(m_volumeTexture.Get(), m_pixelFormat);
      DX::ThrowIfFailed(device->CreateShaderResourceView(m_volumeTexture.Get(), &srvDesc, m_volumeSRV.GetAddressOf()));
#if _DEBUG
      m_volumeSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolumeSRV") - 1, "VolumeSRV");
#endif

      // Compute the step size and number of iterations to use
      //    The step size for each component needs to be a ratio of the largest component
      float maxSize = std::max(m_frameSize[0], std::max(m_frameSize[1], m_frameSize[2]));
      float3 stepSize = float3(1.0f / (m_frameSize[0] * (maxSize / m_frameSize[0])),
                               1.0f / (m_frameSize[1] * (maxSize / m_frameSize[1])),
                               1.0f / (m_frameSize[2] * (maxSize / m_frameSize[2])));

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
    void VolumeEntry::ReleaseVolumeResources()
    {
      m_volumeReady = false;
      m_volumeStagingTexture.Reset();
      m_volumeTexture.Reset();
      m_volumeSRV.Reset();
      m_samplerState.Reset();
    }

    //----------------------------------------------------------------------------
    task<void> VolumeEntry::SetOpacityTransferFunctionTypeAsync(TransferFunctionType functionType, uint32 tableSize, const ControlPointList& controlPoints)
    {
      return create_task([this, functionType, tableSize, controlPoints]()
      {
        std::lock_guard<std::mutex> guard(m_opacityTFMutex);

        delete m_opacityTransferFunction;
        switch (functionType)
        {
          case VolumeEntry::TransferFunction_Piecewise_Linear:
          {
            m_opacityTFType = VolumeEntry::TransferFunction_Piecewise_Linear;
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
    void VolumeEntry::CreateTFResources()
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
    void VolumeEntry::ReleaseTFResources()
    {
      m_tfResourcesReady = false;
      m_opacityLookupTableSRV.Reset();
      m_opacityLookupTableBuffer.Reset();
    }
  }
}