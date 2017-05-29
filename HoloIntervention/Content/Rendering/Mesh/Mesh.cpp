//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

// Local includes
#include "pch.h"
#include "InstancedGeometricPrimitive.h"
#include "Mesh.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// Unnecessary, but removes intellisense errors
#include <WindowsNumerics.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    Mesh::Mesh(std::shared_ptr<DX::DeviceResources> deviceResources)
      : m_deviceResources(deviceResources)
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      ReleaseDeviceDependentResources();
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    Mesh::Mesh()
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      ReleaseDeviceDependentResources();
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    Mesh::~Mesh()
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void Mesh::UpdateSurface(SpatialSurfaceMesh^ surfaceMesh)
    {
      m_surfaceMesh = surfaceMesh;
      m_updateNeeded = true;
    }

    //----------------------------------------------------------------------------
    void Mesh::UpdateDeviceBasedResources()
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      ReleaseDeviceDependentResources();
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    // Spatial Mapping surface meshes each have a transform. This transform is updated every frame.
    void Mesh::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ baseCoordinateSystem)
    {
      if (m_surfaceMesh == nullptr)
      {
        // Not yet ready.
        m_isActive = false;
      }

      if (m_updateNeeded)
      {
        CreateVertexResources();
        m_updateNeeded = false;
      }
      else
      {
        std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

        if (m_updateReady)
        {
          SwapVertexBuffers();
          m_updateReady = false;
        }
      }

      // If the surface is active this frame, we need to update its transform.
      XMMATRIX transform;
      if (m_isActive)
      {
        if (m_colorFadeTimeout > 0.f)
        {
          m_colorFadeTimer += static_cast<float>(timer.GetElapsedSeconds());
          if (m_colorFadeTimer < m_colorFadeTimeout)
          {
            float colorFadeFactor = min(1.f, m_colorFadeTimeout - m_colorFadeTimer);
            m_constantBufferData.colorFadeFactor = XMFLOAT4(colorFadeFactor, colorFadeFactor, colorFadeFactor, 1.f);
          }
          else
          {
            m_constantBufferData.colorFadeFactor = XMFLOAT4(0.f, 0.f, 0.f, 0.f);
            m_colorFadeTimer = m_colorFadeTimeout = -1.f;
          }
        }

        auto tryTransform = m_surfaceMesh->CoordinateSystem->TryGetTransformTo(baseCoordinateSystem);
        if (tryTransform != nullptr)
        {
          transform = XMLoadFloat4x4(&tryTransform->Value);
          m_lastActiveTime = static_cast<float>(timer.GetTotalSeconds());
        }
        else
        {
          m_isActive = false;
        }
      }

      if (!m_isActive)
      {
        return;
      }

      // Set up a transform from surface mesh space, to world space.
      XMMATRIX scaleTransform = XMMatrixScalingFromVector(XMLoadFloat3(&m_surfaceMesh->VertexPositionScale));
      XMStoreFloat4x4(&m_constantBufferData.modelToWorld, scaleTransform * transform);

      // Surface meshes come with normals, which are also transformed from surface mesh space, to world space.
      XMMATRIX normalTransform = transform;
      // Normals are not translated, so we remove the translation component here.
      normalTransform.r[3] = XMVectorSet(0.f, 0.f, 0.f, XMVectorGetW(normalTransform.r[3]));
      XMStoreFloat4x4(&m_constantBufferData.normalToWorld, normalTransform);

      if (!m_constantBufferCreated)
      {
        CreateDeviceDependentResources();
        return;
      }

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_modelTransformBuffer.Get(), 0, NULL, &m_constantBufferData, 0, 0);
    }

    //----------------------------------------------------------------------------
    void Mesh::Render(bool usingVprtShaders)
    {
      if (!m_constantBufferCreated || !m_loadingComplete)
      {
        return;
      }

      if (!m_isActive)
      {
        return;
      }

      auto context = m_deviceResources->GetD3DDeviceContext();

      // The vertices are provided in {vertex, normal} format
      UINT strides[] = { m_meshProperties.vertexStride, m_meshProperties.normalStride };
      UINT offsets[] = { 0, 0 };
      ID3D11Buffer* buffers[] = { m_vertexPositions.Get(), m_vertexNormals.Get() };

      context->IASetVertexBuffers(0, ARRAYSIZE(buffers), buffers, strides, offsets);
      context->IASetIndexBuffer(m_triangleIndices.Get(), m_meshProperties.indexFormat, 0);
      context->VSSetConstantBuffers(0, 1, m_modelTransformBuffer.GetAddressOf());
      if (!usingVprtShaders)
      {
        context->GSSetConstantBuffers(0, 1, m_modelTransformBuffer.GetAddressOf());
      }
      context->PSSetConstantBuffers(0, 1, m_modelTransformBuffer.GetAddressOf());
      context->DrawIndexedInstanced(m_meshProperties.indexCount, 2, 0, 0, 0);
    }

    //----------------------------------------------------------------------------
    void Mesh::CreateDirectXBuffer(ID3D11Device& device, D3D11_BIND_FLAG binding, IBuffer^ buffer, ID3D11Buffer** target)
    {
      auto length = buffer->Length;

      CD3D11_BUFFER_DESC bufferDescription(buffer->Length, binding);
      D3D11_SUBRESOURCE_DATA bufferBytes = { GetDataFromIBuffer(buffer), 0, 0 };
      device.CreateBuffer(&bufferDescription, &bufferBytes, target);
    }

    //----------------------------------------------------------------------------
    void Mesh::CreateVertexResources()
    {
      if (m_surfaceMesh == nullptr)
      {
        m_isActive = false;
        return;
      }

      if (m_surfaceMesh->TriangleIndices->ElementCount < 3)
      {
        m_isActive = false;
        return;
      }

      auto device = m_deviceResources->GetD3DDevice();

      auto taskOptions = Concurrency::task_options();
      auto task = concurrency::create_task([this, device]()
      {
        if (m_surfaceMesh->VertexPositions == nullptr || m_surfaceMesh->VertexNormals == nullptr || m_surfaceMesh->TriangleIndices == nullptr)
        {
          call_after(std::bind(&Mesh::CreateVertexResources, this), 250);
          return;
        }
        IBuffer^ positions = m_surfaceMesh->VertexPositions->Data;
        IBuffer^ normals = m_surfaceMesh->VertexNormals->Data;
        IBuffer^ indices = m_surfaceMesh->TriangleIndices->Data;

        ComPtr<ID3D11Buffer> updatedVertexPositions;
        ComPtr<ID3D11Buffer> updatedVertexNormals;
        ComPtr<ID3D11Buffer> updatedTriangleIndices;
        CreateDirectXBuffer(*device, D3D11_BIND_VERTEX_BUFFER, positions, updatedVertexPositions.GetAddressOf());
        CreateDirectXBuffer(*device, D3D11_BIND_VERTEX_BUFFER, normals, updatedVertexNormals.GetAddressOf());
        CreateDirectXBuffer(*device, D3D11_BIND_INDEX_BUFFER, indices, updatedTriangleIndices.GetAddressOf());

        std::lock_guard<std::mutex> lock(m_meshResourcesMutex);
        auto meshUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;
        if (meshUpdateTime.UniversalTime > m_lastUpdateTime.UniversalTime)
        {
          m_updatedVertexPositions.Swap(updatedVertexPositions);
          m_updatedVertexNormals.Swap(updatedVertexNormals);
          m_updatedTriangleIndices.Swap(updatedTriangleIndices);

          m_updatedMeshProperties.vertexStride = m_surfaceMesh->VertexPositions->Stride;
          m_updatedMeshProperties.normalStride = m_surfaceMesh->VertexNormals->Stride;
          m_updatedMeshProperties.indexCount = m_surfaceMesh->TriangleIndices->ElementCount;
          m_updatedMeshProperties.indexFormat = static_cast<DXGI_FORMAT>(m_surfaceMesh->TriangleIndices->Format);

          m_updateReady = true;
          m_lastUpdateTime = meshUpdateTime;
          m_loadingComplete = true;
        }
      });
    }

    //----------------------------------------------------------------------------
    void Mesh::CreateDeviceDependentResources()
    {
      CreateVertexResources();

      // Create a constant buffer to control mesh position.
      CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ModelNormalConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &constantBufferDesc,
          nullptr,
          &m_modelTransformBuffer
        )
      );

      m_constantBufferCreated = true;
    }

    //----------------------------------------------------------------------------
    void Mesh::ReleaseVertexResources()
    {
      m_loadingComplete = false;

      m_vertexPositions.Reset();
      m_vertexNormals.Reset();
      m_triangleIndices.Reset();

      m_modelTransformBuffer.Reset();
      m_constantBufferCreated = false;
    }

    //----------------------------------------------------------------------------
    void Mesh::SwapVertexBuffers()
    {
      // Swap out the previous vertex position, normal, and index buffers, and replace
      // them with up-to-date buffers.
      m_vertexPositions = m_updatedVertexPositions;
      m_vertexNormals = m_updatedVertexNormals;
      m_triangleIndices = m_updatedTriangleIndices;

      // Swap out the metadata: index count, index format, .
      m_meshProperties = m_updatedMeshProperties;

      ZeroMemory(&m_updatedMeshProperties, sizeof(SurfaceMeshProperties));
      m_updatedVertexPositions.Reset();
      m_updatedVertexNormals.Reset();
      m_updatedTriangleIndices.Reset();
    }

    //----------------------------------------------------------------------------
    void Mesh::ReleaseDeviceDependentResources()
    {
      // Clear out any pending resources.
      SwapVertexBuffers();

      // Clear out active resources.
      ReleaseVertexResources();

      m_modelTransformBuffer.Reset();

      m_constantBufferCreated = false;
      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    bool Mesh::GetIsActive() const
    {
      return m_isActive;
    }

    //----------------------------------------------------------------------------
    float Mesh::GetLastActiveTime() const
    {
      return m_lastActiveTime;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::DateTime Mesh::GetLastUpdateTime() const
    {
      return m_lastUpdateTime;
    }

    //----------------------------------------------------------------------------
    void Mesh::SetIsActive(const bool& isActive)
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    void Mesh::SetColorFadeTimer(const float& duration)
    {
      m_colorFadeTimeout = duration;
      m_colorFadeTimer = 0.f;
    }

    //----------------------------------------------------------------------------
    void Mesh::SetDeviceResources(std::shared_ptr<DX::DeviceResources> deviceResources)
    {
      m_deviceResources = deviceResources;
    }
  }
}