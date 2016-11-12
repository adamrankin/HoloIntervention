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

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Storage;

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
    void VolumeRenderer::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, DX::CameraResources* cameraResources)
    {
      if (frame == m_frame || !m_loadingComplete || !m_tfResourcesReady)
      {
        // nothing to do!
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

      m_transformRepository->SetTransforms(m_frame);
      bool isValid;
      float4x4 transform = m_transformRepository->GetTransform(m_imageToHMDName, &isValid);
      if (!isValid)
      {
        return;
      }

      // TODO : multiply into constant buffer entry
      float4x4 referenceToHMD = HoloIntervention::instance()->GetRegistrationSystem().GetReferenceToHMD();

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
        mappedData += mappedResource.DepthPitch;
        // TODO : does imageData need to be advanced? I don't think so...
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
      DX::ThrowIfFailed(device->CreateShaderResourceView(m_volumeTexture.Get(), nullptr, m_volumeSRV.GetAddressOf()));

      //compute the step size and number of iterations to use
      //the step size for each component needs to be a ratio of the largest component
      float maxSize = std::max(m_frameSize[0], std::max(m_frameSize[1], m_frameSize[2]));
      float3 stepSize = float3(1.0f / (m_frameSize[0] * (maxSize / m_frameSize[0])),
        1.0f / (m_frameSize[1] * (maxSize / m_frameSize[1])),
        1.0f / (m_frameSize[2] * (maxSize / m_frameSize[2])));

      m_constantBuffer.stepSize = stepSize * m_stepScale;
      m_constantBuffer.numIterations = (uint32)(maxSize * (1.0f / m_stepScale) * 2.0f);

      //calculate the scale factor
      //volumes are not always perfect cubes. so we need to scale our cube
      //by the sizes of the volume. Also, scalar data is not always sampled
      //at equidistant steps. So we also need to scale the cube model by mRatios.
      float3 sizes = float3(m_frameSize[0], m_frameSize[1], m_frameSize[2]);
      m_constantBuffer.scaleFactor = float3(1.f, 1.f, 1.f) / ((float3(1.f, 1.f, 1.f) * maxSize) / (sizes * m_ratios));

      m_volumeReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVolumeResources()
    {
      m_volumeReady = false;
      m_volumeStagingTexture.Reset();
      m_volumeTexture.Reset();
      m_volumeSRV.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Render()
    {
      if (!m_loadingComplete || !m_volumeReady || !m_tfResourcesReady)
      {
        return;
      }

      ID3D11DeviceContext3* context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPosition);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      // Cache render target
      ID3D11RenderTargetView* hololensRenderTargetView;
      ID3D11DepthStencilView* hololensStencilView;
      context->OMGetRenderTargets(1, &hololensRenderTargetView, &hololensStencilView);

      // Render pass to render cube position textures used for quick ray calculation
      ID3D11RenderTargetView* targets[1] = { m_frontPositionRTV.Get() };
      context->OMSetRenderTargets(1, targets, nullptr);
      context->VSSetShader(m_volRenderVertexShader.Get(), nullptr, 0);
      context->VSSetConstantBuffers(0, 1, m_volumeConstantBuffer.GetAddressOf());
      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_volRenderGeometryShader.Get(), nullptr, 0);
      }
      context->PSSetShader(m_faceCalcPixelShader.Get(), nullptr, 0);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Set raster state to cull front faces, and perform second pass
      context->RSSetState(m_cullFrontRasterState.Get());
      targets[0] = m_backPositionRTV.Get();
      context->OMSetRenderTargets(1, targets, nullptr);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Clean up after rendering to textures
      context->RSSetState(nullptr);

      // Now perform the actual volume render
      targets[0] = hololensRenderTargetView;
      context->OMSetRenderTargets(1, targets, hololensStencilView);
      ID3D11ShaderResourceView* shaderResourceViews[4] = { m_lookupTableSRV.Get(), m_volumeSRV.Get(), m_frontPositionSRV.Get(), m_backPositionSRV.Get() };
      context->PSSetShaderResources(0, 4, shaderResourceViews);
      context->PSSetConstantBuffers(0, 1, m_volumeConstantBuffer.GetAddressOf());
      context->PSSetShader(m_volRenderPixelShader.Get(), nullptr, 0);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      // Clear values
      ID3D11ShaderResourceView* ppSRVnullptr[4] = { nullptr, nullptr, nullptr, nullptr };
      context->PSSetShaderResources(0, 4, ppSRVnullptr);
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

      CD3D11_RASTERIZER_DESC rasterDesc;
      rasterDesc.FillMode = D3D11_FILL_SOLID;
      rasterDesc.CullMode = D3D11_CULL_FRONT;
      DX::ThrowIfFailed(device->CreateRasterizerState(&rasterDesc, m_cullFrontRasterState.GetAddressOf()));

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

      m_cullFrontRasterState.Reset();
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

      constexpr std::array<uint16_t, 36> cubeIndices =
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

      m_indexCount = cubeIndices.size();

      D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
      indexBufferData.pSysMem = cubeIndices.data();
      indexBufferData.SysMemPitch = 0;
      indexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(cubeIndices), D3D11_BIND_INDEX_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_indexBuffer));

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVertexResources()
    {
      m_loadingComplete = false;

      m_indexBuffer.Reset();
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

      CD3D11_TEXTURE2D_DESC textureDesc(DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(size.Width), static_cast<UINT>(size.Height), 2, 0, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
      m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_frontPositionTextureArray);
      m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_backPositionTextureArray);

      m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_frontPositionTextureArray.Get(), nullptr, m_frontPositionSRV.GetAddressOf());
      m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_backPositionTextureArray.Get(), nullptr, m_backPositionSRV.GetAddressOf());
      m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_frontPositionTextureArray.Get(), nullptr, m_frontPositionRTV.GetAddressOf());
      m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_backPositionTextureArray.Get(), nullptr, m_backPositionRTV.GetAddressOf());

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

      D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
      ZeroMemory(&srvDesc, sizeof(srvDesc));
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
      srvDesc.BufferEx.FirstElement = 0;
      srvDesc.BufferEx.NumElements = TransferFunctionLookup::TRANSFER_FUNCTION_TABLE_SIZE;
      srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_lookupTableBuffer.Get(), &srvDesc, m_lookupTableSRV.GetAddressOf()));

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