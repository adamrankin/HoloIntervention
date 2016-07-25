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

#include "pch.h"

// Local includes
#include "DirectXHelper.h"
#include "GetDataFromIBuffer.h"
#include "SpatialShaderStructures.h"
#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"
#include "SurfaceMesh.h"

// winrt includes
#include <ppltasks.h>

// directx includes
#include <d3dcompiler.h>

using namespace DirectX;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Foundation::Numerics;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh()
    {
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    SurfaceMesh::~SurfaceMesh()
    {
      ReleaseDeviceDependentResources();

      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
      SAFE_RELEASE( m_outputUAV );
      SAFE_RELEASE( m_outputBuffer );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateSurface( SpatialSurfaceMesh^ surfaceMesh, ID3D11Device* device )
    {
      m_surfaceMesh = surfaceMesh;

      auto updateTask = UpdateDeviceBasedResourcesAsync( device ).then( [&]( concurrency::task<void> previousTask )
      {
        try
        {
          previousTask.wait();
        }
        catch ( Platform::Exception^ e )
        {
          OutputDebugStringW( e->Message->Data() );
        }
        catch ( const std::exception& e )
        {
          OutputDebugStringA( e.what() );
        }
      } );
    }

    //----------------------------------------------------------------------------
    // Spatial Mapping surface meshes each have a transform. This transform is updated every frame.
    void SurfaceMesh::UpdateTransform( ID3D11DeviceContext* context, DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem )
    {
      if ( m_surfaceMesh == nullptr )
      {
        // Not yet ready.
        m_isActive = false;
      }

      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      // If the surface is active this frame, we need to update its transform.
      XMMATRIX transform;
      if ( m_isActive )
      {
        // The transform is updated relative to a SpatialCoordinateSystem. In the SurfaceMesh class, we
        // expect to be given the same SpatialCoordinateSystem that will be used to generate view
        // matrices, because this class uses the surface mesh for rendering.
        // Other applications could potentially involve using a SpatialCoordinateSystem from a stationary
        // reference frame that is being used for physics simulation, etc.
        auto tryTransform = m_surfaceMesh->CoordinateSystem->TryGetTransformTo( baseCoordinateSystem );
        if ( tryTransform != nullptr )
        {
          // If the transform can be acquired, this spatial mesh is valid right now and
          // we have the information we need to draw it this frame.
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

      if ( !m_isActive )
      {
        // If for any reason the surface mesh is not active this frame - whether because
        // it was not included in the observer's collection, or because its transform was
        // not located - we don't have the information we need to update it.
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
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateVertexResources( ID3D11Device* device )
    {
      if ( m_surfaceMesh == nullptr )
      {
        m_isActive = false;
        return;
      }

      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
        m_indexCount = m_surfaceMesh->TriangleIndices->ElementCount;

        if ( m_indexCount < 3 )
        {
          m_isActive = false;
          return;
        }
      }

      // Surface mesh resources are created off-thread
      auto task = concurrency::create_task( [this, device]()
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

        SpatialSurfaceMeshBuffer^ positions = m_surfaceMesh->VertexPositions;
        SpatialSurfaceMeshBuffer^ indices = m_surfaceMesh->TriangleIndices;

        auto hr = CreateStructuredBuffer( device, sizeof( VertexBufferType ), positions, &m_vertexPositionBuffer );
        if ( FAILED( hr ) || m_vertexPositionBuffer == NULL )
        {
          throw std::exception( "Unable to create vertex position buffer." );
        }

        hr = CreateStructuredBuffer( device, sizeof( IndexBufferType ), indices, &m_indexBuffer );
        if ( FAILED( hr ) || m_indexBuffer == NULL )
        {
          throw std::exception( "Unable to create index buffer." );
        }

        if ( m_outputBuffer == NULL )
        {
          // Prevent multiple allocations, the output buffer never changes, so don't delete/recreate it
          hr = CreateStructuredBuffer( device, sizeof( OutputBufferType ), 1, &m_outputBuffer );
          if ( FAILED( hr ) || m_outputBuffer == NULL )
          {
            throw std::exception( "Unable to create output result buffer." );
          }

          hr = CreateReadbackBuffer( device, sizeof( OutputBufferType ), 1, &m_readBackBuffer );
          if ( FAILED( hr ) || m_readBackBuffer == NULL )
          {
            throw std::exception( "Unable to create readback buffer." );
          }
        }

#if defined(_DEBUG) || defined(PROFILE)
        if ( m_vertexPositionBuffer )
        {
          m_vertexPositionBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "MeshBuffer" ) - 1, "MeshBuffer" );
        }
        if ( m_indexBuffer )
        {
          m_indexBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "IndexBuffer" ) - 1, "IndexBuffer" );
        }
        if ( m_outputBuffer )
        {
          m_outputBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "OutputBuffer" ) - 1, "OutputBuffer" );
        }
        if ( m_readBackBuffer )
        {
          m_readBackBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "ReadbackBuffer" ) - 1, "ReadbackBuffer" );
        }
#endif

        hr = CreateBufferSRV( *device, *m_vertexPositionBuffer, positions, &m_meshSRV );
        if ( FAILED( hr ) || m_meshSRV == NULL )
        {
          throw std::exception( "Unable to create position shader resource view." );
        }

        hr = CreateBufferSRV( *device, *m_indexBuffer, indices, &m_indexSRV );
        if ( FAILED( hr ) || m_indexSRV == NULL )
        {
          throw std::exception( "Unable to create index shader resource view." );
        }

        CreateBufferUAV( *device, *m_outputBuffer, &m_outputUAV );
        if ( FAILED( hr ) || m_outputUAV == NULL )
        {
          throw std::exception( "Unable to create output access view." );
        }

#if defined(_DEBUG) || defined(PROFILE)
        if ( m_meshSRV )
        {
          m_meshSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Mesh SRV" ) - 1, "Mesh SRV" );
        }
        if ( m_indexSRV )
        {
          m_indexSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Index SRV" ) - 1, "Index SRV" );
        }
        if ( m_outputUAV )
        {
          m_outputUAV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Output UAV" ) - 1, "Output UAV" );
        }
#endif

        m_lastUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;

        m_loadingComplete = true;
      } ).then( [&]( concurrency::task<void> previousTask )
      {
        try
        {
          previousTask.wait();
        }
        catch ( Platform::Exception^ e )
        {
          OutputDebugStringW( e->Message->Data() );
          m_loadingComplete = false;
        }
        catch ( const std::exception& e )
        {
          OutputDebugStringA( e.what() );
          m_loadingComplete = false;
        }
      } );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateDeviceDependentResources( ID3D11Device* device )
    {
      CreateVertexResources( device );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseVertexResources()
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      SAFE_RELEASE( m_meshSRV );
      SAFE_RELEASE( m_indexSRV );
      SAFE_RELEASE( m_outputUAV );
      SAFE_RELEASE( m_vertexPositionBuffer );
      SAFE_RELEASE( m_indexBuffer );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseDeviceDependentResources()
    {
      // Clear out active resources.
      ReleaseVertexResources();

      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayIntersection( ID3D11Device& device,
                                           ID3D11DeviceContext& context,
                                           ID3D11ComputeShader& computeShader,
                                           uint64_t frameNumber,
                                           std::vector<float>& outHitPosition,
                                           std::vector<float>& outHitNormal )
    {
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
        outHitPosition.clear();
        outHitPosition.assign(3, 0.0f);
        outHitNormal.clear();
        outHitNormal.assign(3, 0.0f);

        if ( m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE )
        {
          // Asked twice in the same frame, return the cached result
          std::copy( m_rayIntersectionResults.begin(), m_rayIntersectionResults.begin() + 3, outHitPosition.begin() );
          std::copy( m_rayIntersectionResults.begin() + 3, m_rayIntersectionResults.end(), outHitNormal.begin() );
          return m_hasLastComputedHit;
        }

        // TODO : implement pre-check using OBB
        //m_surfaceMesh->SurfaceInfo->TryGetBounds()

        ID3D11ShaderResourceView* aRViews[2] = { m_meshSRV, m_indexSRV };
        // Send in the number of triangles as the number of thread groups to dispatch
        // triangleCount = m_indexCount/3
        RunComputeShader( context, computeShader, 2, aRViews, m_outputUAV, m_indexCount / 3, 1, 1 );

        context.CopyResource( m_readBackBuffer, m_outputBuffer );
      }

      D3D11_MAPPED_SUBRESOURCE MappedResource;
      OutputBufferType* result;
      context.Map( m_readBackBuffer, 0, D3D11_MAP_READ, 0, &MappedResource );

      result = ( OutputBufferType* )MappedResource.pData;

      context.Unmap( m_readBackBuffer, 0 );

      m_lastFrameNumberComputed = frameNumber;
      m_rayIntersectionResults.clear();
      m_rayIntersectionResults.push_back( result->intersectionPoint[0] );
      m_rayIntersectionResults.push_back( result->intersectionPoint[1] );
      m_rayIntersectionResults.push_back( result->intersectionPoint[2] );
      m_rayIntersectionResults.push_back( result->intersectionNormal[0] );
      m_rayIntersectionResults.push_back( result->intersectionNormal[1] );
      m_rayIntersectionResults.push_back( result->intersectionNormal[2] );

      if ( result->intersectionPoint[0] != 0.0f || result->intersectionPoint[1] != 0.0f || result->intersectionPoint[2] != 0.0f ||
           result->intersectionNormal[0] != 0.0f || result->intersectionNormal[1] != 0.0f || result->intersectionNormal[2] != 0.0f )
      {
        outHitPosition[0] = result->intersectionPoint[0];
        outHitPosition[1] = result->intersectionPoint[1];
        outHitPosition[2] = result->intersectionPoint[2];

        outHitNormal[0] = result->intersectionNormal[0];
        outHitNormal[1] = result->intersectionNormal[1];
        outHitNormal[2] = result->intersectionNormal[2];

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
    void SurfaceMesh::SetIsActive( const bool& isActive )
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetRayConstants( ID3D11DeviceContext* context, ID3D11Buffer* constantBuffer, const float3 rayOrigin, const float3 rayDirection )
    {
      ConstantBuffer cb;
      cb.rayOrigin[0] = rayOrigin.x;
      cb.rayOrigin[1] = rayOrigin.y;
      cb.rayOrigin[2] = rayOrigin.z;
      cb.rayDirection[0] = rayDirection.x;
      cb.rayDirection[1] = rayDirection.y;
      cb.rayDirection[2] = rayDirection.z;
      cb.meshToWorld = m_meshToWorldTransform;

      context->UpdateSubresource( constantBuffer, 0, nullptr, &cb, 0, 0 );
      context->CSSetConstantBuffers( 0, 1, &constantBuffer );
    }

    //----------------------------------------------------------------------------
    concurrency::task<void> SurfaceMesh::UpdateDeviceBasedResourcesAsync( ID3D11Device* device )
    {
      return concurrency::create_task( [ = ]()
      {
        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources( device );
      } );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( ID3D11Device* pDevice,
        uint32 uStructureSize,
        SpatialSurfaceMeshBuffer^ buffer,
        ID3D11Buffer** ppBufOut )
    {
      *ppBufOut = nullptr;

      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = buffer->Data->Length;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uStructureSize;

      D3D11_SUBRESOURCE_DATA bufferBytes = { TrackedUltrasound::GetDataFromIBuffer( buffer->Data ), 0, 0 };
      return pDevice->CreateBuffer( &desc, &bufferBytes, ppBufOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( ID3D11Device* pDevice,
        uint32 uElementSize,
        uint32 uCount,
        ID3D11Buffer** ppBufOut )
    {
      *ppBufOut = nullptr;

      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * uCount;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      return pDevice->CreateBuffer( &desc, nullptr, ppBufOut );
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateReadbackBuffer( ID3D11Device* pDevice, uint32 uElementSize, uint32 uCount, ID3D11Buffer** ppBufOut )
    {
      D3D11_BUFFER_DESC readback_buffer_desc;
      ZeroMemory( &readback_buffer_desc, sizeof( readback_buffer_desc ) );
      readback_buffer_desc.ByteWidth = uElementSize * uCount;
      readback_buffer_desc.Usage = D3D11_USAGE_STAGING;
      readback_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      readback_buffer_desc.StructureByteStride = uElementSize;

      return pDevice->CreateBuffer( &readback_buffer_desc, nullptr, ppBufOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferSRV( ID3D11Device& device,
                                          ID3D11Buffer& computeShaderBuffer,
                                          SpatialSurfaceMeshBuffer^ buffer,
                                          ID3D11ShaderResourceView** ppSRVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer.GetDesc( &descBuf );

      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      desc.BufferEx.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return device.CreateShaderResourceView( &computeShaderBuffer, &desc, ppSRVOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferUAV( ID3D11Device& device,
                                          ID3D11Buffer& computeShaderBuffer,
                                          ID3D11UnorderedAccessView** ppUAVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer.GetDesc( &descBuf );

      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return device.CreateUnorderedAccessView( &computeShaderBuffer, &desc, ppUAVOut );
    }

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::RunComputeShader( ID3D11DeviceContext& context,
                                        ID3D11ComputeShader& computeShader,
                                        uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                                        ID3D11UnorderedAccessView* pUnorderedAccessView,
                                        uint32 xThreadGroups,
                                        uint32 yThreadGroups,
                                        uint32 zThreadGroups )
    {
      OutputBufferType output;
      context.UpdateSubresource( m_outputBuffer, 0, nullptr, &output, 0, 0 );

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
  }
}