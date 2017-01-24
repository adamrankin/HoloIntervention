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
#include "IGTConnector.h"

// DirectX includes
#include <d3d11_3.h>
#include <DirectXMath.h>

// DirectXTex includes
#include <DirectXTex.h>

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
      CreateDeviceDependentResourcesAsync();

      // TODO : change this to read N volume configurations
      /*
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
      */
    }

    //----------------------------------------------------------------------------
    VolumeRenderer::~VolumeRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    uint64 VolumeRenderer::AddVolume(std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat, float4x4 desiredPose)
    {
      if (!m_componentReady)
      {
        throw std::exception("System not ready.");
      }

      std::shared_ptr<VolumeEntry> entry = std::make_shared<VolumeEntry>(m_deviceResources,
                                           m_nextUnusedVolumeToken,
                                           m_cwIndexBuffer.Get(),
                                           m_ccwIndexBuffer.Get(),
                                           m_inputLayout.Get(),
                                           m_vertexBuffer.Get(),
                                           m_volRenderVertexShader.Get(),
                                           m_volRenderGeometryShader.Get(),
                                           m_volRenderPixelShader.Get(),
                                           m_faceCalcPixelShader.Get(),
                                           m_frontPositionTextureArray.Get(),
                                           m_backPositionTextureArray.Get(),
                                           m_frontPositionRTV.Get(),
                                           m_backPositionRTV.Get(),
                                           m_frontPositionSRV.Get(),
                                           m_backPositionSRV.Get());
      entry->SetDesiredPose(desiredPose);
      entry->SetImageData(imageData, width, height, depth, pixelFormat);
      entry->SetShowing(true);

      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      m_volumes.push_back(entry);

      m_nextUnusedVolumeToken++;
      return m_nextUnusedVolumeToken - 1;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::RemoveVolume(uint64 volumeToken)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        for (auto iter = m_volumes.begin(); iter != m_volumes.end(); ++iter)
        {
          if ((*iter)->GetToken() == volumeToken)
          {
            m_volumes.erase(iter);
            return;
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::UpdateVolume(uint64 volumeToken, std::shared_ptr<byte> imageData, uint16 width, uint16 height, uint16 depth, DXGI_FORMAT pixelFormat, float4x4 desiredPose)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->SetDesiredPose(desiredPose);
        entry->SetImageData(imageData, width, height, depth, pixelFormat);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ShowVolume(uint64 volumeToken)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->SetShowing(true);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::HideVolume(uint64 volumeToken)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->SetShowing(false);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::SetVolumeVisible(uint64 volumeToken, bool show)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->SetShowing(show);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::SetVolumePose(uint64 volumeToken, const Windows::Foundation::Numerics::float4x4& pose)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->m_currentPose = entry->m_desiredPose = entry->m_lastPose = pose;
      }
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 VolumeRenderer::GetVolumePose(uint64 volumeToken) const
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        return entry->m_currentPose;
      }

      std::stringstream ss;
      ss << "Unable to locate volume with id: " << volumeToken;
      throw std::exception(ss.str().c_str());
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::SetDesiredVolumePose(uint64 volumeToken, const Windows::Foundation::Numerics::float4x4& pose)
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        entry->SetDesiredPose(pose);
      }
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 VolumeRenderer::GetVolumeVelocity(uint64 volumeToken) const
    {
      std::lock_guard<std::mutex> guard(m_volumeMapMutex);
      std::shared_ptr<VolumeEntry> entry;
      if (FindVolume(volumeToken, entry))
      {
        return entry->GetVelocity();
      }

      std::stringstream ss;
      ss << "Unable to locate volume with id: " << volumeToken;
      throw std::exception(ss.str().c_str());
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, DX::CameraResources* cameraResources, SpatialCoordinateSystem^ hmdCoordinateSystem, SpatialPointerPose^ headPose)
    {
      if (m_cameraResources != cameraResources)
      {
        if (cameraResources != nullptr)
        {
          ReleaseCameraResources();
        }
        m_cameraResources = cameraResources;
        CreateCameraResources();
        m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_volumeRendererConstantBuffer.Get(), 0, nullptr, &m_constantBuffer, 0, 0);
      }

      for (auto& volEntry : m_volumes)
      {
        volEntry->SetImageData(Network::IGTConnector::GetSharedImagePtr(frame), frame->FrameSize->GetAt(0), frame->FrameSize->GetAt(1), frame->FrameSize->GetAt(2), (DXGI_FORMAT)frame->GetPixelFormat(true));
        volEntry->Update(timer);
      }
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Render()
    {
      if (!m_cameraResourcesReady || !m_verticesReady)
      {
        return;
      }

      ID3D11DeviceContext3* context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPosition);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      context->VSSetConstantBuffers(2, 1, m_volumeRendererConstantBuffer.GetAddressOf());
      context->PSSetConstantBuffers(2, 1, m_volumeRendererConstantBuffer.GetAddressOf());

      for (auto& volEntry : m_volumes)
      {
        volEntry->Render(m_indexCount);
      }
    }

    //----------------------------------------------------------------------------
    task<void> VolumeRenderer::CreateDeviceDependentResourcesAsync()
    {
      const auto device = m_deviceResources->GetD3DDevice();

      // load shader code, compile depending on settings requested
      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      CreateVertexResources();

      VolumeEntryConstantBuffer buffer;
      XMStoreFloat4x4(&buffer.worldMatrix, XMMatrixIdentity());
      D3D11_SUBRESOURCE_DATA resData;
      resData.pSysMem = &buffer;
      resData.SysMemPitch = 0;
      resData.SysMemSlicePitch = 0;

      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&CD3D11_BUFFER_DESC(sizeof(VolumeRendererConstantBuffer), D3D11_BIND_CONSTANT_BUFFER), &resData, &m_volumeRendererConstantBuffer));

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

      for (auto& volEntry : m_volumes)
      {
        volEntry->CreateDeviceDependentResources();
      }

      return shaderTaskGroup.then([this]()
      {
        m_componentReady = true;
      });
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseDeviceDependentResources()
    {
      for (auto& volEntry : m_volumes)
      {
        volEntry->ReleaseDeviceDependentResources();
      }

      ReleaseVertexResources();
      ReleaseCameraResources();
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

      m_cameraResourcesReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseCameraResources()
    {
      m_cameraResourcesReady = false;
      m_frontPositionTextureArray.Reset();
      m_backPositionTextureArray.Reset();
      m_frontPositionRTV.Reset();;
      m_backPositionRTV.Reset();
      m_frontPositionSRV.Reset();
      m_backPositionSRV.Reset();
    }

    //----------------------------------------------------------------------------
    bool VolumeRenderer::FindVolume(uint64 volumeToken, std::shared_ptr<VolumeEntry>& volumeEntry) const
    {
      for (auto volume : m_volumes)
      {
        if (volume->GetToken() == volumeToken)
        {
          volumeEntry = volume;
          return true;
        }
      }

      return false;
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
      m_ccwIndexBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("VolRendCcwIndexBuffer") - 1, "VolRendCcwIndexBuffer");
#endif

      m_verticesReady = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVertexResources()
    {
      m_verticesReady = false;

      m_cwIndexBuffer.Reset();
      m_ccwIndexBuffer.Reset();
      m_vertexBuffer.Reset();
    }
  }
}