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
#include "SliceRenderer.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// STL includes
#include <sstream>

// System includes
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// Unnecessary, but eliminates intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    SliceRenderer::SliceRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    SliceRenderer::~SliceRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    uint64 SliceRenderer::AddSlice()
    {
      std::shared_ptr<SliceEntry> entry = std::make_shared<SliceEntry>(m_deviceResources);
      entry->m_id = m_nextUnusedSliceId;
      entry->m_showing = false;
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      m_slices.push_back(entry);

      // Initialize the constant buffer of the slice
      entry->ReleaseDeviceDependentResources();
      entry->CreateDeviceDependentResources();

      m_nextUnusedSliceId++;
      return m_nextUnusedSliceId - 1;
    }

    //----------------------------------------------------------------------------
    uint64 SliceRenderer::AddSlice(std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 desiredPose)
    {
      std::shared_ptr<SliceEntry> entry = std::make_shared<SliceEntry>(m_deviceResources);
      entry->m_id = m_nextUnusedSliceId;

      XMStoreFloat4x4(&entry->m_constantBuffer.worldMatrix, DirectX::XMLoadFloat4x4(&desiredPose));
      entry->m_desiredPose = entry->m_currentPose = entry->m_lastPose = desiredPose;

      entry->SetImageData(imageData, width, height, pixelFormat);
      entry->m_showing = true;

      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      m_slices.push_back(entry);

      m_nextUnusedSliceId++;
      return m_nextUnusedSliceId - 1;
    }

    //----------------------------------------------------------------------------
    uint64 SliceRenderer::AddSlice(IBuffer^ imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 desiredPose)
    {
      std::shared_ptr<SliceEntry> entry = std::make_shared<SliceEntry>(m_deviceResources);
      entry->m_id = m_nextUnusedSliceId;

      XMStoreFloat4x4(&entry->m_constantBuffer.worldMatrix, DirectX::XMLoadFloat4x4(&desiredPose));
      entry->m_desiredPose = entry->m_currentPose = entry->m_lastPose = desiredPose;

      std::shared_ptr<byte> imDataPtr(new byte[imageData->Length], std::default_delete<byte[]>());
      memcpy(imDataPtr.get(), HoloIntervention::GetDataFromIBuffer(imageData), imageData->Length * sizeof(byte));
      entry->SetImageData(imDataPtr, width, height, pixelFormat);
      entry->m_showing = true;

      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      m_slices.push_back(entry);

      m_nextUnusedSliceId++;
      return m_nextUnusedSliceId - 1;
    }

    //----------------------------------------------------------------------------
    uint64 SliceRenderer::AddSlice(const std::wstring& fileName)
    {
      std::shared_ptr<SliceEntry> entry = std::make_shared<SliceEntry>(m_deviceResources);
      entry->m_id = m_nextUnusedSliceId;

      entry->SetImageData(fileName);
      entry->m_showing = true;

      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      m_slices.push_back(entry);

      m_nextUnusedSliceId++;
      return m_nextUnusedSliceId - 1;
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::RemoveSlice(uint64 sliceToken)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> slice;
      if (FindSlice(sliceToken, slice))
      {
        for (auto sliceIter = m_slices.begin(); sliceIter != m_slices.end(); ++sliceIter)
        {
          if ((*sliceIter)->m_id == sliceToken)
          {
            m_slices.erase(sliceIter);
            return;
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<SliceEntry> SliceRenderer::GetSlice(uint64 sliceToken)
    {
      std::shared_ptr<SliceEntry> entry(nullptr);
      FindSlice(sliceToken, entry);
      return entry;
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::UpdateSlice(uint64 sliceToken, std::shared_ptr<byte> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat, float4x4 desiredPose)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->SetDesiredPose(desiredPose);
        entry->SetImageData(imageData, width, height, pixelFormat);
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::ShowSlice(uint64 sliceToken)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->m_showing = true;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::HideSlice(uint64 sliceToken)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->m_showing = false;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetSliceVisible(uint64 sliceToken, bool show)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->m_showing = show;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetSliceHeadlocked(uint64 sliceToken, bool headlocked)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->SetHeadlocked(headlocked);
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetSlicePose(uint64 sliceToken, const float4x4& pose)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->m_currentPose = entry->m_desiredPose = entry->m_lastPose = pose;
      }
    }

    //----------------------------------------------------------------------------
    float4x4 SliceRenderer::GetSlicePose(uint64 sliceToken) const
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        return entry->m_currentPose;
      }

      std::stringstream ss;
      ss << "Unable to locate slice with id: " << sliceToken;
      throw std::exception(ss.str().c_str());
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetDesiredSlicePose(uint64 sliceToken, const float4x4& pose)
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        entry->SetDesiredPose(pose);
      }
    }

    //----------------------------------------------------------------------------
    float3 SliceRenderer::GetSliceVelocity(uint64 sliceToken) const
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);
      std::shared_ptr<SliceEntry> entry;
      if (FindSlice(sliceToken, entry))
      {
        return entry->GetStabilizedVelocity();
      }

      std::stringstream ss;
      ss << "Unable to locate slice with id: " << sliceToken;
      throw std::exception(ss.str().c_str());
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::CreateDeviceDependentResources()
    {
      std::lock_guard<std::mutex> guard(m_sliceMapMutex);

      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(m_usingVprtShaders ? L"ms-appx:///SliceVprtVertexShader.cso" : L"ms-appx:///SliceVertexShader.cso");
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///SlicePixelShader.cso");

      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PTIGeometryShader.cso");
      }

      task<void> createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateVertexShader(fileData.data(), fileData.size(), nullptr, &m_vertexShader));

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 2> vertexDesc =
        {
          {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          }
        };

        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateInputLayout(vertexDesc.data(), vertexDesc.size(), fileData.data(), fileData.size(), &m_inputLayout));
      });

      task<void> createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreatePixelShader(fileData.data(), fileData.size(), nullptr, &m_pixelShader));
      });

      task<void> createGSTask;
      if (!m_usingVprtShaders)
      {
        createGSTask = loadGSTask.then([this](const std::vector<byte>& fileData)
        {
          DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateGeometryShader(fileData.data(), fileData.size(), nullptr, &m_geometryShader));
        });
      }

      float bottom = -0.5;
      float left = -0.5;
      float right = 0.5;
      float top = 0.5;

      std::array<VertexPositionTexture, 4> quadVertices;
      quadVertices[0].pos = XMFLOAT3(left, top, 0.f);
      quadVertices[0].texCoord = XMFLOAT2(0.f, 0.f);
      quadVertices[1].pos = XMFLOAT3(right, top, 0.f);
      quadVertices[1].texCoord = XMFLOAT2(1.f, 0.f);
      quadVertices[2].pos = XMFLOAT3(right, bottom, 0.f);
      quadVertices[2].texCoord = XMFLOAT2(1.f, 1.f);
      quadVertices[3].pos = XMFLOAT3(left, bottom, 0.f);
      quadVertices[3].texCoord = XMFLOAT2(0.f, 1.f);

      D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
      vertexBufferData.pSysMem = quadVertices.data();
      vertexBufferData.SysMemPitch = 0;
      vertexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(VertexPositionTexture) * quadVertices.size(), D3D11_BIND_VERTEX_BUFFER);
      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_vertexBuffer));

      task<void> shaderTaskGroup = m_usingVprtShaders ? (createPSTask && createVSTask) : (createPSTask && createVSTask && createGSTask);
      task<void> finishLoadingTask = shaderTaskGroup.then([this]()
      {
        constexpr std::array<unsigned short, 12> quadIndices =
        {
          {
            // -z
            0, 2, 3,
            0, 1, 2,

            // +z
            2, 0, 3,
            1, 0, 2,
          }
        };

        m_indexCount = quadIndices.size();

        D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
        indexBufferData.pSysMem = quadIndices.data();
        indexBufferData.SysMemPitch = 0;
        indexBufferData.SysMemSlicePitch = 0;
        const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(unsigned short) * quadIndices.size(), D3D11_BIND_INDEX_BUFFER);
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_indexBuffer));

        float borderColour[4] = { 0.f, 0.f, 0.f, 0.f };
        CD3D11_SAMPLER_DESC desc(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP, 0.f, 3, D3D11_COMPARISON_NEVER, borderColour, 0, 3);
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateSamplerState(&desc, &m_quadTextureSamplerState));

        for (auto slice : m_slices)
        {
          slice->CreateDeviceDependentResources();
        }

        m_componentReady = true;
      });
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;
      m_inputLayout.Reset();
      m_indexBuffer.Reset();
      m_vertexBuffer.Reset();
      m_vertexShader.Reset();
      m_geometryShader.Reset();
      m_pixelShader.Reset();

      for (auto slice : m_slices)
      {
        slice->ReleaseDeviceDependentResources();
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::Update(SpatialPointerPose^ pose, const DX::StepTimer& timer)
    {
      for (auto slice : m_slices)
      {
        slice->Update(pose, timer);
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::Render()
    {
      if (!m_componentReady)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      std::lock_guard<std::mutex> guard(m_sliceMapMutex);

      const UINT stride = sizeof(VertexPositionTexture);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_geometryShader.Get(), nullptr, 0);
      }
      context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      context->PSSetSamplers(0, 1, m_quadTextureSamplerState.GetAddressOf());

      // TODO : implement instance rendering (instance of instance?)
      for (auto sliceEntry : m_slices)
      {
        sliceEntry->Render(m_indexCount);
      }

      ID3D11SamplerState* ppNullptr[1] = { nullptr };
      context->PSSetSamplers(0, 1, ppNullptr);
    }

    //----------------------------------------------------------------------------
    bool SliceRenderer::FindSlice(uint64 sliceToken, std::shared_ptr<SliceEntry>& sliceEntry) const
    {
      for (auto slice : m_slices)
      {
        if (slice->m_id == sliceToken)
        {
          sliceEntry = slice;
          return true;
        }
      }

      return false;
    }
  }
}