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
#include "SurfaceMesh.h"
#include "SpatialSurfaceCollection.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// WinRT includes
#include <ppltasks.h>

// DirectX includes
#include <d3dcompiler.h>
#include <DirectXMath.h>

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      ReleaseDeviceDependentResources();
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    SurfaceMesh::~SurfaceMesh()
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateSurface(SpatialSurfaceMesh^ newMesh)
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);
      m_surfaceMesh = newMesh;
      m_updateNeeded = true;
    }

    //----------------------------------------------------------------------------
    Surfaces::SpatialSurfaceMesh^ SurfaceMesh::GetSurfaceMesh()
    {
      return m_surfaceMesh;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::Update(DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem)
    {
      if (baseCoordinateSystem == nullptr)
      {
        return;
      }

      if (m_surfaceMesh == nullptr)
      {
        m_isActive = false;
        return;
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

      XMMATRIX transform;
      if (m_isActive && m_surfaceMesh->CoordinateSystem != nullptr)
      {
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
      XMStoreFloat4x4(&m_meshToWorldTransform, scaleTransform * transform);

      // Surface meshes come with normals, which are also transformed from surface mesh space, to world space.
      XMMATRIX normalTransform = transform;
      // Normals are not translated, so we remove the translation component here.
      normalTransform.r[3] = XMVectorSet(0.f, 0.f, 0.f, XMVectorGetW(normalTransform.r[3]));
      XMStoreFloat4x4(&m_normalToWorldTransform, normalTransform);

      if (!m_loadingComplete)
      {
        CreateDeviceDependentResources();
        return;
      }
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateVertexResources()
    {
      if (m_surfaceMesh == nullptr)
      {
        m_isActive = false;
        return;
      }

      m_indexCount = m_surfaceMesh->TriangleIndices->ElementCount;

      if (m_indexCount < 3)
      {
        m_isActive = false;
        return;
      }

      SpatialSurfaceMeshBuffer^ positions = m_surfaceMesh->VertexPositions;
      SpatialSurfaceMeshBuffer^ indices = m_surfaceMesh->TriangleIndices;

      Microsoft::WRL::ComPtr<ID3D11Buffer> updatedVertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer> updatedTriangleIndices;
      DX::ThrowIfFailed(CreateStructuredBuffer(sizeof(VertexBufferType), positions, updatedVertexPositions.GetAddressOf()));
#if _DEBUG
      updatedVertexPositions->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("updatedVertexPositions") - 1, "updatedVertexPositions");
#endif
      DX::ThrowIfFailed(CreateStructuredBuffer(sizeof(IndexBufferType), indices, updatedTriangleIndices.GetAddressOf()));
#if _DEBUG
      updatedTriangleIndices->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("updatedTriangleIndices") - 1, "updatedTriangleIndices");
#endif

      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> updatedVertexPositionsSRV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> updatedTriangleIndicesSRV;
      DX::ThrowIfFailed(CreateBufferSRV(updatedVertexPositions, updatedVertexPositionsSRV.GetAddressOf()));
#if _DEBUG
      updatedVertexPositionsSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("updatedVertexPositionsSRV") - 1, "updatedVertexPositionsSRV");
#endif
      DX::ThrowIfFailed(CreateBufferSRV(updatedTriangleIndices, updatedTriangleIndicesSRV.GetAddressOf()));
#if _DEBUG
      updatedTriangleIndicesSRV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("updatedTriangleIndicesSRV") - 1, "updatedTriangleIndicesSRV");
#endif

      // Before updating the meshes, check to ensure that there wasn't a more recent update.
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      auto meshUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;
      if (meshUpdateTime.UniversalTime > m_lastUpdateTime.UniversalTime)
      {
        // Here, we use ComPtr.Swap() to avoid unnecessary overhead from ref counting.
        m_updatedVertexPositions.Swap(updatedVertexPositions);
        m_updatedTriangleIndices.Swap(updatedTriangleIndices);

        m_updatedVertexSRV.Swap(updatedVertexPositionsSRV);
        m_updatedIndicesSRV.Swap(updatedTriangleIndicesSRV);

        // Cache properties for the buffers we will now use.
        m_updatedMeshProperties.vertexStride = m_surfaceMesh->VertexPositions->Stride;
        m_updatedMeshProperties.indexCount = m_surfaceMesh->TriangleIndices->ElementCount;
        m_updatedMeshProperties.indexFormat = static_cast<DXGI_FORMAT>(m_surfaceMesh->TriangleIndices->Format);

        // Send a signal to the render loop indicating that new resources are available to use.
        m_updateReady = true;
        m_lastUpdateTime = meshUpdateTime;
        m_vertexLoadingComplete = true;
      }
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateDeviceDependentResources()
    {
      CreateVertexResources();

      DX::ThrowIfFailed(CreateStructuredBuffer(sizeof(OutputBufferType), 1, m_outputBuffer.GetAddressOf()));
#if _DEBUG
      m_outputBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("m_outputBuffer") - 1, "m_outputBuffer");
#endif
      DX::ThrowIfFailed(CreateReadbackBuffer(sizeof(OutputBufferType), 1));
#if _DEBUG
      m_readBackBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("m_readBackBuffer") - 1, "m_readBackBuffer");
#endif
      DX::ThrowIfFailed(CreateConstantBuffer());
#if _DEBUG
      m_meshConstantBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("m_meshConstantBuffer") - 1, "m_meshConstantBuffer");
#endif
      DX::ThrowIfFailed(CreateBufferUAV(m_outputBuffer, m_outputUAV.GetAddressOf()));
#if _DEBUG
      m_outputUAV->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("m_outputUAV") - 1, "m_outputUAV");
#endif

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseVertexResources()
    {
      m_vertexPositions.Reset();
      m_triangleIndices.Reset();
      m_vertexSRV.Reset();
      m_indexSRV.Reset();

      m_vertexLoadingComplete = false;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseDeviceDependentResources()
    {
      // Clear out any pending resources.
      SwapVertexBuffers();

      // Clear out active resources.
      ReleaseVertexResources();

      // Clear out active resources.
      m_outputUAV.Reset();
      m_outputBuffer.Reset();
      m_readBackBuffer.Reset();
      m_meshConstantBuffer.Reset();

      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SwapVertexBuffers()
    {
      // Swap out the previous vertex position, normal, and index buffers, and replace
      // them with up-to-date buffers.
      m_vertexPositions = m_updatedVertexPositions;
      m_triangleIndices = m_updatedTriangleIndices;
      m_vertexSRV = m_updatedVertexSRV;
      m_indexSRV = m_updatedIndicesSRV;

      // Swap out the metadata: index count, index format, .
      m_meshProperties = m_updatedMeshProperties;

      ZeroMemory(&m_updatedMeshProperties, sizeof(SurfaceMeshProperties));
      m_updatedVertexPositions.Reset();
      m_updatedTriangleIndices.Reset();
      m_updatedVertexSRV.Reset();
      m_updatedIndicesSRV.Reset();
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayIntersection(ID3D11DeviceContext& context,
                                          uint64_t frameNumber,
                                          float3& outHitPosition,
                                          float3& outHitNormal,
                                          float3& outHitEdge)
    {
      std::lock_guard<std::mutex> lock(m_meshResourcesMutex);

      if (!m_vertexLoadingComplete || !m_loadingComplete)
      {
        return false;
      }

      WorldConstantBuffer buffer;
      XMStoreFloat4x4(&buffer.meshToWorld, XMLoadFloat4x4(&m_meshToWorldTransform));
      context.UpdateSubresource(m_meshConstantBuffer.Get(), 0, nullptr, &buffer, 0, 0);
      context.CSSetConstantBuffers(0, 1, m_meshConstantBuffer.GetAddressOf());

      if (m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE)
      {
        // Asked twice in the frame period, return the cached result
        outHitPosition = m_lastHitPosition;
        outHitNormal = m_lastHitNormal;
        outHitEdge = m_lastHitEdge;
        return m_hasLastComputedHit;
      }

      ID3D11ShaderResourceView* shaderResourceViews[2] = { m_vertexSRV.Get(), m_indexSRV.Get() };
      // Send in the number of triangles as the number of thread groups to dispatch
      // triangleCount = m_indexCount/3
      RunComputeShader(context, 2, shaderResourceViews, m_outputUAV.Get(), m_indexCount / 3, 1, 1);

      context.CopyResource(m_readBackBuffer.Get(), m_outputBuffer.Get());

      D3D11_MAPPED_SUBRESOURCE MappedResource;
      OutputBufferType* result;
      context.Map(m_readBackBuffer.Get(), 0, D3D11_MAP_READ, 0, &MappedResource);

      result = (OutputBufferType*)MappedResource.pData;

      context.Unmap(m_readBackBuffer.Get(), 0);

      m_lastFrameNumberComputed = frameNumber;

      ID3D11Buffer* ppCBnullptr[1] = { nullptr };
      context.CSSetConstantBuffers(0, 1, ppCBnullptr);

      if (result->intersection)
      {
        outHitPosition = m_lastHitPosition = float3(result->intersectionPoint.x, result->intersectionPoint.y, result->intersectionPoint.z);
        outHitNormal = m_lastHitNormal = float3(result->intersectionNormal.x, result->intersectionNormal.y, result->intersectionNormal.z);
        outHitEdge = m_lastHitEdge = float3(result->intersectionEdge.x, result->intersectionEdge.y, result->intersectionEdge.z);
        m_hasLastComputedHit = true;
        return true;
      }

      m_hasLastComputedHit = false;
      return false;
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::GetIsActive() const
    {
      return m_isActive;
    }

    //----------------------------------------------------------------------------
    float SurfaceMesh::GetLastActiveTime() const
    {
      return m_lastActiveTime;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::DateTime& SurfaceMesh::GetLastUpdateTime() const
    {
      return m_lastUpdateTime;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& SurfaceMesh::GetLastHitPosition() const
    {
      if (m_hasLastComputedHit)
      {
        return m_lastHitPosition;
      }

      throw new std::exception("No hit ever recorded.");
    }

    //----------------------------------------------------------------------------
    const float3& SurfaceMesh::GetLastHitNormal() const
    {
      return m_lastHitNormal;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& SurfaceMesh::GetLastHitEdge() const
    {
      return m_lastHitEdge;
    }

    //----------------------------------------------------------------------------
    uint64_t SurfaceMesh::GetLastHitFrameNumber() const
    {
      return m_lastFrameNumberComputed;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetIsActive(const bool& isActive)
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    float4x4 SurfaceMesh::GetMeshToWorldTransform()
    {
      return m_meshToWorldTransform;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ComputeOBBInverseWorld(SpatialCoordinateSystem^ baseCoordinateSystem)
    {
      if (m_lastWorldToBoxComputedCoordSystem == baseCoordinateSystem)
      {
        return;
      }

      if (m_surfaceMesh == nullptr)
      {
        return;
      }

      if (m_surfaceMesh->SurfaceInfo == nullptr)
      {
        throw std::exception("Mesh surface info not available.");
      }
      Platform::IBox<SpatialBoundingOrientedBox>^ bounds = m_surfaceMesh->SurfaceInfo->TryGetBounds(baseCoordinateSystem);

      if (bounds == nullptr)
      {
        throw std::exception("Cannot compute bounds.");
      }

      //float4x4 boxToWorld = make_float4x4_scale(bounds->Value.Extents) * make_float4x4_from_quaternion(bounds->Value.Orientation) * make_float4x4_translation(bounds->Value.Center);
      float4x4 m_worldToBoxCenterTransform = make_float4x4_scale(float3(1.f, 1.f, 1.f) / bounds->Value.Extents) * make_float4x4_from_quaternion(inverse(bounds->Value.Orientation)) * make_float4x4_translation(-bounds->Value.Center);

      m_lastWorldToBoxComputedCoordSystem = baseCoordinateSystem;
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer(uint32 uStructureSize, SpatialSurfaceMeshBuffer^ buffer, ID3D11Buffer** target)
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = buffer->Data->Length;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uStructureSize;

      D3D11_SUBRESOURCE_DATA bufferBytes = { HoloIntervention::GetDataFromIBuffer(buffer->Data), 0, 0 };
      return m_deviceResources->GetD3DDevice()->CreateBuffer(&desc, &bufferBytes, target);
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer(uint32 uElementSize, uint32 uCount, ID3D11Buffer** target)
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * uCount;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer(&desc, nullptr, target);
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateReadbackBuffer(uint32 uElementSize, uint32 uCount)
    {
      D3D11_BUFFER_DESC readback_buffer_desc;
      ZeroMemory(&readback_buffer_desc, sizeof(readback_buffer_desc));
      readback_buffer_desc.ByteWidth = uElementSize * uCount;
      readback_buffer_desc.Usage = D3D11_USAGE_STAGING;
      readback_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      readback_buffer_desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer(&readback_buffer_desc, nullptr, &m_readBackBuffer);
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferSRV(ComPtr<ID3D11Buffer> computeShaderBuffer, ID3D11ShaderResourceView** ppSRVOut)
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory(&descBuf, sizeof(descBuf));
      computeShaderBuffer->GetDesc(&descBuf);

      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      desc.BufferEx.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateShaderResourceView(computeShaderBuffer.Get(), &desc, ppSRVOut);
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferUAV(ComPtr<ID3D11Buffer> computeShaderBuffer, ID3D11UnorderedAccessView** ppUAVOut)
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory(&descBuf, sizeof(descBuf));
      computeShaderBuffer->GetDesc(&descBuf);

      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      ZeroMemory(&desc, sizeof(desc));
      desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateUnorderedAccessView(computeShaderBuffer.Get(), &desc, ppUAVOut);
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateConstantBuffer()
    {
      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory(&constant_buffer_desc, sizeof(constant_buffer_desc));
      constant_buffer_desc.ByteWidth = sizeof(WorldConstantBuffer);
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;

      auto hr = m_deviceResources->GetD3DDevice()->CreateBuffer(&constant_buffer_desc, nullptr, &m_meshConstantBuffer);
      if (FAILED(hr))
      {
        return hr;
      }

      return hr;
    }

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::RunComputeShader(ID3D11DeviceContext& context, uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews, ID3D11UnorderedAccessView* pUnorderedAccessView, uint32 xThreadGroups, uint32 yThreadGroups, uint32 zThreadGroups)
    {
      if (!m_vertexLoadingComplete)
      {
        return;
      }
      OutputBufferType output;
      output.intersection = false;
      context.UpdateSubresource(m_outputBuffer.Get(), 0, nullptr, &output, 0, 0);

      context.CSSetShaderResources(0, nNumViews, pShaderResourceViews);
      context.CSSetUnorderedAccessViews(0, 1, &pUnorderedAccessView, nullptr);
      context.Dispatch(xThreadGroups, yThreadGroups, zThreadGroups);

      ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
      context.CSSetUnorderedAccessViews(0, 1, ppUAViewnullptr, nullptr);

      ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
      context.CSSetShaderResources(0, 2, ppSRVnullptr);
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayOBBIntersection(SpatialCoordinateSystem^ desiredCoordinateSystem, uint64_t frameNumber, const float3& rayOrigin, const float3& rayDirection)
    {
      if (m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE)
      {
        return m_hasLastComputedHit;
      }

      try
      {
        ComputeOBBInverseWorld(desiredCoordinateSystem);
      }
      catch (const std::exception&)
      {
        return false;
      }

      float3 rayBox = transform(rayOrigin, m_worldToBoxCenterTransform);
      float4x4 rotateScale = m_worldToBoxCenterTransform;
      rotateScale.m41 = 0.f;
      rotateScale.m42 = 0.f;
      rotateScale.m43 = 0.f;
      float3 rayDirBox = transform(rayDirection, rotateScale);
      rayDirBox = normalize(rayDirBox);

      float3 rayInvDirBox;
      rayInvDirBox.x = 1.f / rayDirBox.x;
      rayInvDirBox.y = 1.f / rayDirBox.y;
      rayInvDirBox.z = 1.f / rayDirBox.z;

      // Algorithm implementation derived from
      // https://tavianator.com/cgit/dimension.git/tree/libdimension/bvh/bvh.c
      // thanks to Tavian Barnes <tavianator@tavianator.com>

      float tx1 = (-0.5f - rayBox.x) * rayInvDirBox.x;
      float tx2 = (0.5f - rayBox.x) * rayInvDirBox.x;

      float tmin = std::fmin(tx1, tx2);
      float tmax = std::fmax(tx1, tx2);

      float ty1 = (-0.5f - rayBox.y) * rayInvDirBox.y;
      float ty2 = (0.5f - rayBox.y) * rayInvDirBox.y;

      tmin = std::fmax(tmin, std::fmin(ty1, ty2));
      tmax = std::fmin(tmax, std::fmax(ty1, ty2));

      float tz1 = (-0.5f - rayBox.z) * rayInvDirBox.z;
      float tz2 = (0.5f - rayBox.z) * rayInvDirBox.z;

      tmin = std::fmax(tmin, std::fmin(tz1, tz2));
      tmax = std::fmin(tmax, std::fmax(tz1, tz2));

      return tmax >= std::fmax(0.0, tmin);
    }
  }
}