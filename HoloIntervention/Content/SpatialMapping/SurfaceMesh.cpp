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

#include "pch.h"

// Local includes
#include "Common.h"
#include "DirectXHelper.h"
#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"
#include "SurfaceMesh.h"

// WinRT includes
#include <ppltasks.h>
#include <WindowsNumerics.h>

// DirectX includes
#include <d3dcompiler.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      ReleaseDeviceDependentResources();
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    SurfaceMesh::~SurfaceMesh()
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateSurface( SpatialSurfaceMesh^ newMesh )
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
      m_worldToBoxTransformComputed = false;
      m_surfaceMesh = newMesh;
      m_updateNeeded = true;
    }

    //----------------------------------------------------------------------------
    Surfaces::SpatialSurfaceMesh^ SurfaceMesh::GetSurfaceMesh()
    {
      return m_surfaceMesh;
    }

    //----------------------------------------------------------------------------
    // Spatial Mapping surface meshes each have a transform. This transform is updated every frame.
    void SurfaceMesh::Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem )
    {
      if ( baseCoordinateSystem == nullptr )
      {
        return;
      }

      if ( m_surfaceMesh == nullptr )
      {
        // Not yet ready.
        m_isActive = false;
      }

      if ( m_updateNeeded )
      {
        CreateVertexResources();
        m_updateNeeded = false;
      }
      else
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

        if ( m_updateReady )
        {
          // Surface mesh resources are created off-thread so that they don't affect rendering latency.
          // When a new update is ready, we should begin using the updated vertex position, normal, and
          // index buffers.
          SwapVertexBuffers();
          m_updateReady = false;
        }
      }

      // If the surface is active this frame, we need to update its transform.
      XMMATRIX transform;
      if ( m_isActive && m_surfaceMesh->CoordinateSystem != nullptr )
      {
        auto tryTransform = m_surfaceMesh->CoordinateSystem->TryGetTransformTo( baseCoordinateSystem );
        if ( tryTransform != nullptr )
        {
          // If the transform can be acquired, this spatial mesh is valid right now
          transform = XMLoadFloat4x4( &tryTransform->Value );
          m_lastActiveTime = static_cast<float>( timer.GetTotalSeconds() );
        }
        else
        {
          // If the transform is not acquired, the spatial mesh is not valid right now
          // because its location cannot be correlated to the current space.
          m_isActive = false;
        }
      }

      if ( !m_worldToBoxTransformComputed )
      {
        ComputeOBBInverseWorld( baseCoordinateSystem );
      }

      if ( !m_isActive )
      {
        return;
      }

      // Set up a transform from surface mesh space, to world space.
      XMMATRIX scaleTransform = XMMatrixScalingFromVector( XMLoadFloat3( &m_surfaceMesh->VertexPositionScale ) );
      XMStoreFloat4x4(
        &m_meshToWorldTransform,
        XMMatrixTranspose( scaleTransform * transform )
      );

      // Surface meshes come with normals, which are also transformed from surface mesh space, to world space.
      XMMATRIX normalTransform = transform;
      // Normals are not translated, so we remove the translation component here.
      normalTransform.r[3] = XMVectorSet( 0.f, 0.f, 0.f, XMVectorGetW( normalTransform.r[3] ) );
      XMStoreFloat4x4(
        &m_normalToWorldTransform,
        XMMatrixTranspose( normalTransform )
      );

      if ( !m_loadingComplete )
      {
        // If loading is not yet complete, we cannot actually update the graphics resources.
        // This return is intentionally placed after the surface mesh updates so that this
        // code may be copied and re-used for CPU-based processing of surface data.
        CreateDeviceDependentResources();
        return;
      }
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateVertexResources()
    {
      if ( m_surfaceMesh == nullptr )
      {
        m_isActive = false;
        return;
      }

      m_indexCount = m_surfaceMesh->TriangleIndices->ElementCount;

      if ( m_indexCount < 3 )
      {
        m_isActive = false;
        return;
      }

      SpatialSurfaceMeshBuffer^ positions = m_surfaceMesh->VertexPositions;
      SpatialSurfaceMeshBuffer^ indices = m_surfaceMesh->TriangleIndices;

      Microsoft::WRL::ComPtr<ID3D11Buffer> updatedVertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer> updatedTriangleIndices;
      DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( VertexBufferType ), positions, updatedVertexPositions.GetAddressOf() ) );
      DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( IndexBufferType ), indices, updatedTriangleIndices.GetAddressOf() ) );

      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> updatedVertexPositionsSRV;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> updatedTriangleIndicesSRV;
      DX::ThrowIfFailed( CreateBufferSRV( updatedVertexPositions, positions, updatedVertexPositionsSRV.GetAddressOf() ) );
      DX::ThrowIfFailed( CreateBufferSRV( updatedTriangleIndices, indices, updatedTriangleIndicesSRV.GetAddressOf() ) );

      // Before updating the meshes, check to ensure that there wasn't a more recent update.
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

        auto meshUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;
        if ( meshUpdateTime.UniversalTime > m_lastUpdateTime.UniversalTime )
        {
          // Prepare to swap in the new meshes.
          // Here, we use ComPtr.Swap() to avoid unnecessary overhead from ref counting.
          m_updatedVertexPositions.Swap( updatedVertexPositions );
          m_updatedTriangleIndices.Swap( updatedTriangleIndices );

          m_updatedVertexSRV.Swap( updatedVertexPositionsSRV );
          m_updatedIndicesSRV.Swap( updatedTriangleIndicesSRV );

          // Cache properties for the buffers we will now use.
          m_updatedMeshProperties.vertexStride = m_surfaceMesh->VertexPositions->Stride;
          m_updatedMeshProperties.indexCount = m_surfaceMesh->TriangleIndices->ElementCount;
          m_updatedMeshProperties.indexFormat = static_cast<DXGI_FORMAT>( m_surfaceMesh->TriangleIndices->Format );

          // Send a signal to the render loop indicating that new resources are available to use.
          m_updateReady = true;
          m_lastUpdateTime = meshUpdateTime;
          m_vertexLoadingComplete = true;
        }
      }
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateDeviceDependentResources()
    {
      CreateVertexResources();

      DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( OutputBufferType ), 1, m_outputBuffer.GetAddressOf() ) );
      DX::ThrowIfFailed( CreateReadbackBuffer( sizeof( OutputBufferType ), 1 ) );
      DX::ThrowIfFailed( CreateConstantBuffer() );
      DX::ThrowIfFailed( CreateBufferUAV( m_outputBuffer, m_outputUAV.GetAddressOf() ) );

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

      ZeroMemory( &m_updatedMeshProperties, sizeof( SurfaceMeshProperties ) );
      m_updatedVertexPositions.Reset();
      m_updatedTriangleIndices.Reset();
      m_updatedVertexSRV.Reset();
      m_updatedIndicesSRV.Reset();
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayIntersection( ID3D11DeviceContext& context,
                                           uint64_t frameNumber,
                                           float3& outHitPosition,
                                           float3& outHitNormal,
                                           float3& outHitEdge )
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      if ( !m_vertexLoadingComplete )
      {
        return false;
      }

      WorldConstantBuffer buffer;
      XMStoreFloat4x4( &buffer.meshToWorld, XMLoadFloat4x4( &m_meshToWorldTransform ) );
      context.UpdateSubresource( m_meshConstantBuffer.Get(), 0, nullptr, &buffer, 0, 0 );
      context.CSSetConstantBuffers( 0, 1, m_meshConstantBuffer.GetAddressOf() );

      if ( m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE )
      {
        // Asked twice in the frame period, return the cached result
        outHitPosition = m_lastHitPosition;
        outHitNormal = m_lastHitNormal;
        outHitEdge = m_lastHitEdge;
        return m_hasLastComputedHit;
      }

      ID3D11ShaderResourceView* aRViews[2] = { m_vertexSRV.Get(), m_indexSRV.Get() };
      // Send in the number of triangles as the number of thread groups to dispatch
      // triangleCount = m_indexCount/3
      RunComputeShader( context, 2, aRViews, m_outputUAV.Get(), m_indexCount / 3, 1, 1 );

      context.CopyResource( m_readBackBuffer.Get(), m_outputBuffer.Get() );

      D3D11_MAPPED_SUBRESOURCE MappedResource;
      OutputBufferType* result;
      context.Map( m_readBackBuffer.Get(), 0, D3D11_MAP_READ, 0, &MappedResource );

      result = ( OutputBufferType* )MappedResource.pData;

      context.Unmap( m_readBackBuffer.Get(), 0 );

      m_lastFrameNumberComputed = frameNumber;

      if ( result->intersection )
      {
        outHitPosition = m_lastHitPosition = float3( result->intersectionPoint.x, result->intersectionPoint.y, result->intersectionPoint.z );
        outHitNormal = m_lastHitNormal = float3( result->intersectionNormal.x, result->intersectionNormal.y, result->intersectionNormal.z );
        outHitEdge = m_lastHitEdge = float3( result->intersectionEdge.x, result->intersectionEdge.y, result->intersectionEdge.z );
        m_hasLastComputedHit = true;
        return true;
      }

      m_hasLastComputedHit = false;
      return false;
    }

    //----------------------------------------------------------------------------
    const bool& SurfaceMesh::GetIsActive() const
    {
      return m_isActive;
    }

    //----------------------------------------------------------------------------
    const float& SurfaceMesh::GetLastActiveTime() const
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
      if ( m_hasLastComputedHit )
      {
        return m_lastHitPosition;
      }

      throw new std::exception( "No hit ever recorded." );
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
    void SurfaceMesh::SetIsActive( const bool& isActive )
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ComputeOBBInverseWorld( SpatialCoordinateSystem^ baseCoordinateSystem )
    {
      m_worldToBoxTransformComputed = false;

      if ( m_surfaceMesh == nullptr )
      {
        return;
      }

      Platform::IBox<SpatialBoundingOrientedBox>^ bounds = m_surfaceMesh->SurfaceInfo->TryGetBounds( baseCoordinateSystem );

      if ( bounds != nullptr )
      {
        XMMATRIX rotation = XMMatrixTranspose( XMLoadFloat4x4( &make_float4x4_from_quaternion( bounds->Value.Orientation ) ) );
        XMMATRIX scale = XMLoadFloat4x4( &make_float4x4_scale( 2 * bounds->Value.Extents ) );
        XMMATRIX translation = XMMatrixTranspose( XMLoadFloat4x4( &make_float4x4_translation( bounds->Value.Center ) ) );
        XMMATRIX worldTransform = translation * ( rotation * scale );
        XMVECTOR determinant = XMMatrixDeterminant( worldTransform );
        m_worldToBoxTransform = XMMatrixInverse( &determinant, worldTransform );

        m_worldToBoxTransformComputed = true;
      }
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( uint32 uStructureSize, SpatialSurfaceMeshBuffer^ buffer, ID3D11Buffer** target )
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = buffer->Data->Length;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uStructureSize;

      D3D11_SUBRESOURCE_DATA bufferBytes = { HoloIntervention::GetDataFromIBuffer( buffer->Data ), 0, 0 };
      return m_deviceResources->GetD3DDevice()->CreateBuffer( &desc, &bufferBytes, target );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( uint32 uElementSize, uint32 uCount, ID3D11Buffer** target )
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * uCount;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer( &desc, nullptr, target );
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateReadbackBuffer( uint32 uElementSize, uint32 uCount )
    {
      D3D11_BUFFER_DESC readback_buffer_desc;
      ZeroMemory( &readback_buffer_desc, sizeof( readback_buffer_desc ) );
      readback_buffer_desc.ByteWidth = uElementSize * uCount;
      readback_buffer_desc.Usage = D3D11_USAGE_STAGING;
      readback_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      readback_buffer_desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer( &readback_buffer_desc, nullptr, &m_readBackBuffer );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferSRV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                                          SpatialSurfaceMeshBuffer^ buffer,
                                          ID3D11ShaderResourceView** ppSRVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer->GetDesc( &descBuf );

      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      desc.BufferEx.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateShaderResourceView( computeShaderBuffer.Get(), &desc, ppSRVOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferUAV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                                          ID3D11UnorderedAccessView** ppUAVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer->GetDesc( &descBuf );

      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateUnorderedAccessView( computeShaderBuffer.Get(), &desc, ppUAVOut );
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateConstantBuffer()
    {
      // Create the Const Buffer
      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory( &constant_buffer_desc, sizeof( constant_buffer_desc ) );
      constant_buffer_desc.ByteWidth = sizeof( WorldConstantBuffer );
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;

      auto hr = m_deviceResources->GetD3DDevice()->CreateBuffer( &constant_buffer_desc, nullptr, &m_meshConstantBuffer );
      if ( FAILED( hr ) )
      {
        return hr;
      }

#if defined(_DEBUG) || defined(PROFILE)
      m_meshConstantBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "RayConstantBuffer" ) - 1, "RayConstantBuffer" );
#endif

      return hr;
    }

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::RunComputeShader( ID3D11DeviceContext& context,
                                        uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                                        ID3D11UnorderedAccessView* pUnorderedAccessView,
                                        uint32 xThreadGroups,
                                        uint32 yThreadGroups,
                                        uint32 zThreadGroups )
    {
      if ( !m_vertexLoadingComplete )
      {
        return;
      }
      OutputBufferType output;
      output.intersection = false;
      context.UpdateSubresource( m_outputBuffer.Get(), 0, nullptr, &output, 0, 0 );

      // Set the shaders resources
      context.CSSetShaderResources( 0, nNumViews, pShaderResourceViews );
      context.CSSetUnorderedAccessViews( 0, 1, &pUnorderedAccessView, nullptr );

      // The total number of thread groups creates is x*y*z, the number of threads in a thread group is determined by numthreads(i,j,k) in the shader code
      context.Dispatch( xThreadGroups, yThreadGroups, zThreadGroups );

      ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
      context.CSSetUnorderedAccessViews( 0, 1, ppUAViewnullptr, nullptr );

      ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
      context.CSSetShaderResources( 0, 2, ppSRVnullptr );

      ID3D11Buffer* ppCBnullptr[1] = { nullptr };
      context.CSSetConstantBuffers( 0, 1, ppCBnullptr );
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayOBBIntersection( SpatialCoordinateSystem^ desiredCoordinateSystem,
        uint64_t frameNumber,
        const float3& rayOrigin,
        const float3& rayDirection )
    {
      if ( m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE )
      {
        // Asked before the threshold for recompute has happened, returned cached value
        return m_hasLastComputedHit;
      }

      if ( m_surfaceMesh->SurfaceInfo == nullptr )
      {
        // Can't tell, so have to run the compute shader to verify
        return true;
      }
      else
      {
        Platform::IBox<SpatialBoundingOrientedBox>^ bounds = m_surfaceMesh->SurfaceInfo->TryGetBounds( desiredCoordinateSystem );

        if ( bounds == nullptr || !m_worldToBoxTransformComputed )
        {
          // Can't tell, so have to run the compute shader to verify
          return true;
        }

        // Use inverse world to transform ray, then compare against unit AABB (cent 0,0,0 ext 1,1,1)
        XMVECTOR rayBox = XMVector3Transform( XMLoadFloat3( &rayOrigin ), m_worldToBoxTransform );
        XMVECTOR rayDirBox = XMVector3Transform( XMLoadFloat3( &rayDirection ), m_worldToBoxTransform );

        XMVECTOR rayInvDirBox;
        rayInvDirBox.m128_f32[0] = 1.f / rayDirBox.m128_f32[0];
        rayInvDirBox.m128_f32[1] = 1.f / rayDirBox.m128_f32[1];
        rayInvDirBox.m128_f32[2] = 1.f / rayDirBox.m128_f32[2];
        rayInvDirBox.m128_f32[3] = 1.f;

        // Algorithm implementation derived from
        // https://tavianator.com/cgit/dimension.git/tree/libdimension/bvh/bvh.c
        // thanks to Tavian Barnes <tavianator@tavianator.com>
        float xMin = -0.5f;
        float xMax = 0.5;
        float yMin = -0.5f;
        float yMax = 0.5f;
        float zMin = -0.5f;
        float zMax = 0.5;

        float tx1 = ( xMin - rayBox.m128_f32[0] ) * rayInvDirBox.m128_f32[0];
        float tx2 = ( xMax - rayBox.m128_f32[0] ) * rayInvDirBox.m128_f32[0];

        float tmin = min( tx1, tx2 );
        float tmax = max( tx1, tx2 );

        float ty1 = ( yMin - rayBox.m128_f32[1] ) * rayInvDirBox.m128_f32[1];
        float ty2 = ( yMax - rayBox.m128_f32[1] ) * rayInvDirBox.m128_f32[1];

        tmin = max( tmin, min( ty1, ty2 ) );
        tmax = min( tmax, max( ty1, ty2 ) );

        float tz1 = ( zMin - rayBox.m128_f32[2] ) * rayInvDirBox.m128_f32[2];
        float tz2 = ( zMax - rayBox.m128_f32[2] ) * rayInvDirBox.m128_f32[2];

        tmin = max( tmin, min( tz1, tz2 ) );
        tmax = min( tmax, max( tz1, tz2 ) );

        return tmax >= max( 0.0, tmin );
      }
    }
  }
}