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
#include "VolumeRenderer.h"
#include "Common.h"

// Common includes
#include "DeviceResources.h"
#include "StepTimer.h"
#include "DirectXHelper.h"

// System includes
#include "RegistrationSystem.h"
#include "NotificationSystem.h"

// Network includes
#include "IGTLinkIF.h"

// DirectX includes
#include <d3d11_3.h>
#include <DirectXMath.h>
#include <DirectXTex.h>
#include <DirectXColors.h>

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    VolumeRenderer::VolumeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      try
      {
        InitializeTransformRepositoryAsync(m_transformRepository, L"Assets\\Data\\configuration.xml");
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(e->Message);
      }

      CreateDeviceDependentResourcesAsync();

      std::vector<float2> points;
      points.push_back(float2(0.f, 0.f));
      points.push_back(float2(255.f, 1.f));
      SetTransferFunctionTypeAsync(TransferFunction_Piecewise_Linear, points);

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
    }

    //----------------------------------------------------------------------------
    VolumeRenderer::~VolumeRenderer()
    {
      delete m_transferFunction;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, DX::CameraResources* cameraResources, SpatialCoordinateSystem^ hmdCoordinateSystem, SpatialPointerPose^ headPose)
    {
      if (frame == m_frame || !m_componentReady || !m_tfResourcesReady)
      {
        // nothing to do!
        return;
      }

      // Frame sanity check
      if (frame->ImageData == nullptr || frame->FrameSize->GetAt(0) == 0 || frame->FrameSize->GetAt(1) == 0 || frame->FrameSize->GetAt(2) == 0)
      {
        return;
      }

      m_frame = frame;

      if (m_cameraResources != cameraResources)
      {
        m_cameraResources = cameraResources;
        CreateCameraResources();
      }

      auto context = m_deviceResources->GetD3DDeviceContext();
      auto device = m_deviceResources->GetD3DDevice();

      uint16 frameSize[3] = { frame->FrameSize->GetAt(0), frame->FrameSize->GetAt(1), frame->FrameSize->GetAt(2) };

      if (!m_volumeReady && frameSize[2] > 1)
      {
        ReleaseVolumeResources();
        CreateVolumeResources();
      }
      else if (frameSize[0] != m_frameSize[0] || frameSize[1] != m_frameSize[1] || m_frameSize[2] != frameSize[2])
      {
        ReleaseVolumeResources();
        CreateVolumeResources();
      }
      else if (frameSize[2] < 2)
      {
        // No depth, nothing to volume render
        return;
      }
      else
      {
        UpdateGPUImageData();
      }

      // Retrieve the current registration from reference to HMD
      m_transformRepository->SetTransforms(m_frame);
      float4x4 trackerToHMD = HoloIntervention::instance()->GetRegistrationSystem().GetTrackerToCoordinateSystemTransformation(hmdCoordinateSystem);
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(trackerToHMD), true);
      bool isValid;
      float4x4 transform = transpose(m_transformRepository->GetTransform(m_imageToHMDName, &isValid));
      if (!isValid)
      {
        return;
      }

      // Temporary debug code
      const float PI = 3.14159265359f;
      float3 pos = headPose->Head->Position + (2.f * headPose->Head->ForwardDirection);
      // Create pixel to meter scaling factor
      transform = make_float4x4_scale(0.5f / 11.2f) * make_float4x4_rotation_y(23.f * PI / 180.f) * make_float4x4_translation(pos);

      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMLoadFloat4x4(&transform));
      context->UpdateSubresource(m_volumeConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::UpdateGPUImageData()
    {
      const auto context = m_deviceResources->GetD3DDeviceContext();

      auto bytesPerPixel = BitsPerPixel((DXGI_FORMAT)m_frame->PixelFormat) / 8;
      auto imagePtr = Network::IGTLinkIF::GetSharedImagePtr(m_frame);

      // Map image resource and update data
      D3D11_MAPPED_SUBRESOURCE mappedResource;
      context->Map(m_volumeStagingTexture.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mappedResource);
      byte* imageData = imagePtr.get();
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
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::CreateVolumeResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_frame == nullptr)
      {
        return;
      }

      m_frameSize[0] = m_frame->FrameSize->GetAt(0);
      m_frameSize[1] = m_frame->FrameSize->GetAt(1);
      m_frameSize[2] = m_frame->FrameSize->GetAt(2);

      auto bytesPerPixel = BitsPerPixel((DXGI_FORMAT)m_frame->PixelFormat) / 8;

      // Create a staging texture that will be used to copy data from the CPU to the GPU,
      // the staging texture will then copy to the render texture
      CD3D11_TEXTURE3D_DESC textureDesc((DXGI_FORMAT)m_frame->PixelFormat, m_frameSize[0], m_frameSize[1], m_frameSize[2], 1, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
      D3D11_SUBRESOURCE_DATA imgData;
      imgData.pSysMem = Network::IGTLinkIF::GetSharedImagePtr(m_frame).get();
      imgData.SysMemPitch = m_frameSize[0] * bytesPerPixel;
      imgData.SysMemSlicePitch = m_frameSize[0] * m_frameSize[1] * bytesPerPixel;
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeStagingTexture.GetAddressOf()));

      // Create the texture that will be used by the shader to access the current volume to be rendered
      textureDesc = CD3D11_TEXTURE3D_DESC((DXGI_FORMAT)m_frame->PixelFormat, m_frameSize[0], m_frameSize[1], m_frameSize[2], 1);
      DX::ThrowIfFailed(device->CreateTexture3D(&textureDesc, &imgData, m_volumeTexture.GetAddressOf()));
#if _DEBUG
      m_volumeTexture->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolumeTexture") - 1, "VolumeTexture");
#endif
      CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(m_volumeTexture.Get(), (DXGI_FORMAT)m_frame->PixelFormat);
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

      m_constantBuffer.stepSize = stepSize * m_stepScale;
      m_constantBuffer.numIterations = (uint32)(maxSize * (1.0f / m_stepScale));

      float borderColour[4] = { 0.f, 0.f, 0.f, 0.f };
      CD3D11_SAMPLER_DESC desc(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER, D3D11_TEXTURE_ADDRESS_BORDER, 0.f, 3, D3D11_COMPARISON_NEVER, borderColour, 0, 3);
      DX::ThrowIfFailed(device->CreateSamplerState(&desc, m_samplerState.GetAddressOf()));
#if _DEBUG
      m_samplerState->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendSamplerState") - 1, "VolRendSamplerState");
#endif

      m_volumeReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVolumeResources()
    {
      m_volumeReady = false;
      m_volumeStagingTexture.Reset();
      m_volumeTexture.Reset();
      m_volumeSRV.Reset();
      m_samplerState.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Render()
    {
      if (!m_componentReady || !m_volumeReady || !m_tfResourcesReady)
      {
        return;
      }

      ID3D11DeviceContext3* context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPosition);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      // Cache render target
      ID3D11RenderTargetView* hololensRenderTargetView;
      ID3D11DepthStencilView* hololensStencilView;
      context->OMGetRenderTargets(1, &hololensRenderTargetView, &hololensStencilView);

      context->ClearRenderTargetView(m_frontPositionRTV.Get(), DirectX::Colors::Black);
      context->ClearRenderTargetView(m_backPositionRTV.Get(), DirectX::Colors::Black);

      context->RSSetState(nullptr);

      // Set index buffer to cw winding to calculate front faces
      context->IASetIndexBuffer(m_cwIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      ID3D11RenderTargetView* targets[1] = { m_frontPositionRTV.Get() };
      context->OMSetRenderTargets(1, targets, nullptr);
      context->VSSetShader(m_volRenderVertexShader.Get(), nullptr, 0);
      context->VSSetConstantBuffers(0, 1, m_volumeConstantBuffer.GetAddressOf());
      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_volRenderGeometryShader.Get(), nullptr, 0);
        context->GSSetConstantBuffers(0, 1, m_volumeConstantBuffer.GetAddressOf());
      }
      context->PSSetShader(m_faceCalcPixelShader.Get(), nullptr, 0);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Set index buffer to ccw winding to calculate back faces
      context->IASetIndexBuffer(m_ccwIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      targets[0] = m_backPositionRTV.Get();
      context->OMSetRenderTargets(1, targets, nullptr);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Now perform the actual volume render
      targets[0] = hololensRenderTargetView;
      context->OMSetRenderTargets(1, targets, hololensStencilView);
      context->IASetIndexBuffer(m_cwIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      ID3D11ShaderResourceView* shaderResourceViews[4] = { m_lookupTableSRV.Get(), m_volumeSRV.Get(), m_frontPositionSRV.Get(), m_backPositionSRV.Get() };
      context->PSSetShaderResources(0, 4, shaderResourceViews);
      ID3D11SamplerState* samplerStates[1] = { m_samplerState.Get() };
      context->PSSetSamplers(0, 1, samplerStates);
      context->PSSetConstantBuffers(0, 1, m_volumeConstantBuffer.GetAddressOf());
      context->PSSetShader(m_volRenderPixelShader.Get(), nullptr, 0);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Clear values
      ID3D11ShaderResourceView* ppSRVnullptr[4] = { nullptr, nullptr, nullptr, nullptr };
      context->PSSetShaderResources(0, 4, ppSRVnullptr);
      ID3D11SamplerState* ppSamplerStatesnullptr[1] = { nullptr };
      context->PSSetSamplers(0, 1, ppSamplerStatesnullptr);
    }

    //----------------------------------------------------------------------------
    task<void> VolumeRenderer::SetTransferFunctionTypeAsync(TransferFunctionType type, const std::vector<float2>& controlPoints)
    {
      return create_task([this, type, controlPoints]()
      {
        std::lock_guard<std::mutex> guard(m_tfMutex);

        delete m_transferFunction;
        switch (type)
        {
        case VolumeRenderer::TransferFunction_Piecewise_Linear:
        default:
          m_tfType = VolumeRenderer::TransferFunction_Piecewise_Linear;
          m_transferFunction = new PiecewiseLinearTF();
          break;
        }

        for (auto& point : controlPoints)
        {
          m_transferFunction->AddControlPoint(point);
        }

        m_transferFunction->Update();
      }).then([this]()
      {
        std::lock_guard<std::mutex> guard(m_tfMutex);
        ReleaseTFResources();
        CreateTFResources();
      });
    }

    //----------------------------------------------------------------------------
    task<void> VolumeRenderer::CreateDeviceDependentResourcesAsync()
    {
      // load shader code, compile depending on settings requested
      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      const auto device = m_deviceResources->GetD3DDevice();

      if (m_tfType != TransferFunction_Unknown)
      {
        std::lock_guard<std::mutex> guard(m_tfMutex);
        ReleaseTFResources();
        CreateTFResources();
      }

      if (m_frame != nullptr)
      {
        ReleaseVolumeResources();
        CreateVolumeResources();
      }

      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(m_usingVprtShaders ? L"ms-appx:///VolumeRendererVprtVS.cso" : L"ms-appx:///VolumeRendererVS.cso");
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///VolumeRendererPS.cso");
      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PT0T1IGeometryShader.cso");
      }

      task<std::vector<byte>> loadFacePSTask = DX::ReadDataAsync(L"ms-appx:///FaceAnalysisPS.cso");
      task<void> createVSTask = loadVSTask.then([this, device](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(device->CreateVertexShader(fileData.data(), fileData.size(), nullptr, &m_volRenderVertexShader));
        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 1> vertexDesc =
        {
          {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          }
        };
        DX::ThrowIfFailed(device->CreateInputLayout(vertexDesc.data(), vertexDesc.size(), fileData.data(), fileData.size(), &m_inputLayout));
      });

      task<void> createPSTask = loadPSTask.then([this, device](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(device->CreatePixelShader(fileData.data(), fileData.size(), nullptr, &m_volRenderPixelShader));
#if _DEBUG
        m_volRenderPixelShader->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRenderPixelShader") - 1, "VolRenderPixelShader");
#endif

        VolumeConstantBuffer buffer;
        XMStoreFloat4x4(&buffer.worldMatrix, XMMatrixIdentity());
        buffer.lt_maximumXValue = m_transferFunction->GetTFLookupTable().MaximumXValue;
        D3D11_SUBRESOURCE_DATA resData;
        resData.pSysMem = &buffer;
        resData.SysMemPitch = 0;
        resData.SysMemSlicePitch = 0;

        CD3D11_BUFFER_DESC constantBufferDesc(sizeof(VolumeConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
        DX::ThrowIfFailed(device->CreateBuffer(&constantBufferDesc, &resData, &m_volumeConstantBuffer));
      });
      task<void> createFacePSTask = loadFacePSTask.then([this, device](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(device->CreatePixelShader(fileData.data(), fileData.size(), nullptr, &m_faceCalcPixelShader));
#if _DEBUG
        m_faceCalcPixelShader->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("FaceCalcPixelShader") - 1, "FaceCalcPixelShader");
#endif
      });

      task<void> createGSTask;
      task<void> createFaceGSTask;
      if (!m_usingVprtShaders)
      {
        // After the geometry shader file is loaded, create the shader.
        createGSTask = loadGSTask.then([this, device](const std::vector<byte>& fileData)
        {
          DX::ThrowIfFailed(device->CreateGeometryShader(fileData.data(), fileData.size(), nullptr, &m_volRenderGeometryShader));
        });
      }

      // Once all shaders are loaded, create the mesh.
      task<void> shaderTaskGroup = m_usingVprtShaders
                                   ? (createPSTask && createVSTask && createFacePSTask)
                                   : (createPSTask && createVSTask && createGSTask && createFacePSTask);

      task<void> finishLoadingTask = shaderTaskGroup.then([this]()
      {
        CreateVertexResources();
      });

      return finishLoadingTask;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseDeviceDependentResources()
    {
      ReleaseVertexResources();
      ReleaseCameraResources();
      ReleaseTFResources();

      m_faceCalcPixelShader.Reset();
      m_volRenderVertexShader.Reset();
      m_inputLayout.Reset();
      m_volRenderPixelShader.Reset();
      m_volRenderGeometryShader.Reset();
      m_volumeConstantBuffer.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::CreateVertexResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      static const std::array<VertexPosition, 8> cubeVertices =
      {
        {
          { float3(0.f, 0.f, 0.f), },
          { float3(0.f, 0.f,  1.f), },
          { float3(0.f,  1.f, 0.f), },
          { float3(0.f,  1.f,  1.f), },
          { float3(1.f, 0.f, 0.f), },
          { float3(1.f, 0.f,  1.f), },
          { float3(1.f,  1.f, 0.f), },
          { float3(1.f,  1.f,  1.f), },
        }
      };

      D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
      vertexBufferData.pSysMem = cubeVertices.data();
      vertexBufferData.SysMemPitch = 0;
      vertexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(cubeVertices), D3D11_BIND_VERTEX_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_vertexBuffer));
#if _DEBUG
      m_vertexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendVertexBuffer") - 1, "VolRendVertexBuffer");
#endif

      constexpr std::array<uint16_t, 36> cwCubeIndices =
      {
        {
          2, 1, 0, // -x
          2, 3, 1,

          6, 4, 5, // +x
          6, 5, 7,

          0, 1, 5, // -y
          0, 5, 4,

          2, 6, 7, // +y
          2, 7, 3,

          0, 4, 6, // -z
          0, 6, 2,

          1, 3, 7, // +z
          1, 7, 5,
        }
      };

      m_indexCount = cwCubeIndices.size();

      D3D11_SUBRESOURCE_DATA cwIndexBufferData = { 0 };
      cwIndexBufferData.pSysMem = cwCubeIndices.data();
      cwIndexBufferData.SysMemPitch = 0;
      cwIndexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC cwIndexBufferDesc(sizeof(cwCubeIndices), D3D11_BIND_INDEX_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&cwIndexBufferDesc, &cwIndexBufferData, &m_cwIndexBuffer));
#if _DEBUG
      m_cwIndexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendCwIndexBuffer") - 1, "VolRendCwIndexBuffer");
#endif

      constexpr std::array<uint16_t, 36> ccwCubeIndices =
      {
        {
          0, 1, 2, // -x
          1, 3, 2,

          5, 4, 6, // +x
          7, 5, 6,

          5, 1, 0, // -y
          4, 5, 0,

          7, 6, 2, // +y
          3, 7, 2,

          6, 4, 0, // -z
          2, 6, 0,

          7, 3, 1, // +z
          5, 7, 1,
        }
      };
      D3D11_SUBRESOURCE_DATA ccwIndexBufferData = { 0 };
      ccwIndexBufferData.pSysMem = ccwCubeIndices.data();
      ccwIndexBufferData.SysMemPitch = 0;
      ccwIndexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC ccwIndexBufferDesc(sizeof(ccwCubeIndices), D3D11_BIND_INDEX_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&ccwIndexBufferDesc, &ccwIndexBufferData, &m_ccwIndexBuffer));
#if _DEBUG
      m_cwIndexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendCcwIndexBuffer") - 1, "VolRendCcwIndexBuffer");
#endif

      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVertexResources()
    {
      m_componentReady = false;

      m_cwIndexBuffer.Reset();
      m_ccwIndexBuffer.Reset();
      m_vertexBuffer.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::CreateCameraResources()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      if (m_cameraResources == nullptr)
      {
        return;
      }

      const auto size = m_cameraResources->GetRenderTargetSize();

      m_constantBuffer.viewportDimensions.x = size.Width;
      m_constantBuffer.viewportDimensions.y = size.Height;

      CD3D11_TEXTURE2D_DESC textureDesc(DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 2, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
      m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_frontPositionTextureArray);
#if _DEBUG
      m_frontPositionTextureArray->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("FrontFaceArray") - 1, "FrontFaceArray");
#endif
      m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_backPositionTextureArray);
#if _DEBUG
      m_backPositionTextureArray->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("BackFaceArray") - 1, "BackFaceArray");
#endif

      CD3D11_SHADER_RESOURCE_VIEW_DESC frontSrvDesc(m_frontPositionTextureArray.Get(), D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
      m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_frontPositionTextureArray.Get(), &frontSrvDesc, m_frontPositionSRV.GetAddressOf());
#if _DEBUG
      m_frontPositionSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("FrontFaceSRV") - 1, "FrontFaceSRV");
#endif
      CD3D11_SHADER_RESOURCE_VIEW_DESC backSrvDesc(m_backPositionTextureArray.Get(), D3D11_SRV_DIMENSION_TEXTURE2DARRAY);
      m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_backPositionTextureArray.Get(), &backSrvDesc, m_backPositionSRV.GetAddressOf());
#if _DEBUG
      m_backPositionSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("BackFaceSRV") - 1, "BackFaceSRV");
#endif

      CD3D11_RENDER_TARGET_VIEW_DESC frontRendDesc(m_frontPositionTextureArray.Get(), D3D11_RTV_DIMENSION_TEXTURE2DARRAY);
      m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_frontPositionTextureArray.Get(), &frontRendDesc, m_frontPositionRTV.GetAddressOf());
#if _DEBUG
      m_frontPositionRTV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("FrontFaceRTV") - 1, "FrontFaceRTV");
#endif
      CD3D11_RENDER_TARGET_VIEW_DESC backRendDesc(m_backPositionTextureArray.Get(), D3D11_RTV_DIMENSION_TEXTURE2DARRAY);
      m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_backPositionTextureArray.Get(), &backRendDesc, m_backPositionRTV.GetAddressOf());
#if _DEBUG
      m_backPositionRTV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("BackFaceRTV") - 1, "BackFaceRTV");
#endif

      m_faceCalcReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseCameraResources()
    {
      m_faceCalcReady = false;
      m_frontPositionTextureArray.Reset();
      m_backPositionTextureArray.Reset();
      m_frontPositionRTV.Reset();;
      m_backPositionRTV.Reset();
      m_frontPositionSRV.Reset();
      m_backPositionSRV.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::CreateTFResources()
    {
      if (m_transferFunction == nullptr)
      {
        return;
      }

      if (!m_transferFunction->IsValid())
      {
        throw new std::exception("Transfer function table not valid.");
      }

      m_transferFunction->Update();
      m_constantBuffer.lt_maximumXValue = m_transferFunction->GetTFLookupTable().MaximumXValue;

      // Set up GPU memory
      D3D11_BUFFER_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = sizeof(float) * TransferFunctionLookup::TRANSFER_FUNCTION_TABLE_SIZE;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
      desc.StructureByteStride = sizeof(float);

      D3D11_SUBRESOURCE_DATA bufferBytes = { m_transferFunction->GetTFLookupTable().LookupTable, 0, 0 };
      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&desc, &bufferBytes, m_lookupTableBuffer.GetAddressOf()));
#if _DEBUG
      m_lookupTableBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("LookupTable") - 1, "LookupTable");
#endif

      CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(m_lookupTableBuffer.Get(), DXGI_FORMAT_R32_TYPELESS, 0, TransferFunctionLookup::TRANSFER_FUNCTION_TABLE_SIZE, D3D11_BUFFEREX_SRV_FLAG_RAW);
      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_lookupTableBuffer.Get(), &srvDesc, m_lookupTableSRV.GetAddressOf()));
#if _DEBUG
      m_lookupTableSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("LookupTableSRV") - 1, "LookupTableSRV");
#endif

      m_tfResourcesReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseTFResources()
    {
      m_tfResourcesReady = false;
      m_lookupTableSRV.Reset();
      m_lookupTableBuffer.Reset();
    }
  }
}