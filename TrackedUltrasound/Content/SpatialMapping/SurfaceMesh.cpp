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

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p)=nullptr; } }
#endif

namespace TrackedUltrasound
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh()
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

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
    void SurfaceMesh::UpdateSurface( SpatialSurfaceMesh^ surfaceMesh, ID3D11Device* device )
    {
      m_surfaceMesh = surfaceMesh;

      auto updateTask = UpdateDeviceBasedResourcesAsync(device);
    }

    //----------------------------------------------------------------------------
    concurrency::task<void> SurfaceMesh::UpdateDeviceBasedResourcesAsync( ID3D11Device* device )
    {
      return concurrency::create_task( [ = ]()
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources( device );
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

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::SetConstants( ID3D11DeviceContext* context, const std::vector<double>& rayOrigin, const std::vector<double>& rayDirection )
    {
      ConstantBuffer cb;
      context->UpdateSubresource( m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0 );
      context->CSSetConstantBuffers( 0, 1, &m_constantBuffer );
    }

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::RunComputeShader( ID3D11DeviceContext* pContext,
                                        ID3D11ComputeShader* pComputeShader,
                                        uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                                        ID3D11Buffer* pCBCS,
                                        void* pCSData,
                                        DWORD dwNumDataBytes,
                                        ID3D11UnorderedAccessView* pUnorderedAccessView,
                                        uint32 Xthreads,
                                        uint32 Ythreads,
                                        uint32 Zthreads )
    {
      pContext->CSSetShader( pComputeShader, nullptr, 0 );
      pContext->CSSetShaderResources( 0, nNumViews, pShaderResourceViews );
      pContext->CSSetUnorderedAccessViews( 0, 1, &pUnorderedAccessView, nullptr );
      if ( pCBCS && pCSData )
      {
        D3D11_MAPPED_SUBRESOURCE MappedResource;
        pContext->Map( pCBCS, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedResource );
        memcpy( MappedResource.pData, pCSData, dwNumDataBytes );
        pContext->Unmap( pCBCS, 0 );
        ID3D11Buffer* ppCB[1] = { pCBCS };
        pContext->CSSetConstantBuffers( 0, 1, ppCB );
      }

      pContext->Dispatch( Xthreads, Ythreads, Zthreads );

      pContext->CSSetShader( nullptr, nullptr, 0 );

      ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
      pContext->CSSetUnorderedAccessViews( 0, 1, ppUAViewnullptr, nullptr );

      ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
      pContext->CSSetShaderResources( 0, 2, ppSRVnullptr );

      ID3D11Buffer* ppCBnullptr[1] = { nullptr };
      pContext->CSSetConstantBuffers( 0, 1, ppCBnullptr );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateVertexResources( ID3D11Device* device )
    {
      if ( m_surfaceMesh == nullptr )
      {
        m_isActive = false;
        return;
      }

      if ( m_surfaceMesh->TriangleIndices->ElementCount < 3 )
      {
        m_isActive = false;
        return;
      }

      // Surface mesh resources are created off-thread
      auto taskOptions = Concurrency::task_options();
      auto task = concurrency::create_task( [this, device]()
      {
        auto createTask = CreateComputeShaderAsync( L"ms-appx:///CSRayTriangleIntersection.cso", "CSMain", device, &m_d3d11ComputeShader );
        if( FAILED( createTask.get() ) )
        {
          throw std::exception( "Cannot create compute shader. Aborting." );
        }

        Windows::Storage::Streams::IBuffer^ positions = m_surfaceMesh->VertexPositions->Data;
        Windows::Storage::Streams::IBuffer^ normals = m_surfaceMesh->VertexNormals->Data;
        Windows::Storage::Streams::IBuffer^ indices = m_surfaceMesh->TriangleIndices->Data;

        CreateStructuredBuffer( device, sizeof( InputBufferType ), positions, m_meshBuffer.GetAddressOf() );
        CreateStructuredBuffer( device, sizeof( InputBufferType ), normals, m_normalBuffer.GetAddressOf() );
        CreateStructuredBuffer( device, sizeof( OutputBufferType ), 1, m_outputBuffer.GetAddressOf() );

#if defined(_DEBUG) || defined(PROFILE)
        if ( m_meshBuffer )
        { m_meshBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "MeshBuffer" ) - 1, "MeshBuffer" ); }
        if ( m_normalBuffer )
        { m_normalBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "NormalBuffer" ) - 1, "NormalBuffer" ); }
        if ( m_outputBuffer )
        { m_outputBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "OutputBuffer" ) - 1, "OutputBuffer" ); }
#endif

        CreateBufferSRV( device, m_meshBuffer.Get(), &m_meshSRV );
        CreateBufferSRV( device, m_normalBuffer.Get(), &m_normalSRV );
        CreateBufferUAV( device, m_outputBuffer.Get(), &m_outputUAV );

#if defined(_DEBUG) || defined(PROFILE)
        if ( m_meshSRV )
        { m_meshSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Mesh SRV" ) - 1, "Mesh SRV" ); }
        if ( m_normalSRV )
        { m_normalSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Normal SRV" ) - 1, "Normal SRV" ); }
        if ( m_outputUAV )
        { m_outputUAV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Output UAV" ) - 1, "Output UAV" ); }
#endif

        m_lastUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;

        m_loadingComplete = true;
      } ).then( [&]( concurrency::task<void> previousTask )
      {
        try
        {
          previousTask.wait();
        }
        catch ( const std::exception& e )
        {
          OutputDebugStringA( e.what() );
          m_loadingComplete = false;
          return;
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
      m_meshSRV = nullptr;
      m_normalSRV = nullptr;
      m_outputUAV = nullptr;
      m_meshBuffer = nullptr;
      m_normalBuffer = nullptr;
      m_outputBuffer = nullptr;
      m_d3d11ComputeShader = nullptr;
    }

    //--------------------------------------------------------------------------------------
    concurrency::task<HRESULT> SurfaceMesh::CreateComputeShaderAsync( const std::wstring& srcFile,
        const std::string& functionName,
        ID3D11Device* pDevice,
        ID3D11ComputeShader** ppShaderOut )
    {
      concurrency::task<std::vector<byte>> loadVSTask = DX::ReadDataAsync( srcFile );
      return loadVSTask.then( [ = ]( std::vector<byte> data )
      {
        if ( !pDevice || !ppShaderOut )
        {
          return E_INVALIDARG;
        }

        HRESULT hr = pDevice->CreateComputeShader( &data.front(), data.size(), nullptr, ppShaderOut );

#if defined(_DEBUG) || defined(PROFILE)
        if ( SUCCEEDED( hr ) )
        {
          ( *ppShaderOut )->SetPrivateData( WKPDID_D3DDebugObjectName, functionName.length(), functionName.c_str() );
        }
#endif
        return hr;
      } );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( ID3D11Device* pDevice,
        uint32 uElementSize,
        IBuffer^ buffer,
        ID3D11Buffer** ppBufOut )
    {
      *ppBufOut = nullptr;

      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * buffer->Length;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      D3D11_SUBRESOURCE_DATA bufferBytes = { TrackedUltrasound::GetDataFromIBuffer( buffer ), 0, 0 };
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
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * uCount;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      return pDevice->CreateBuffer( &desc, nullptr, ppBufOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferSRV( ID3D11Device* pDevice,
                                          ID3D11Buffer* pBuffer,
                                          ID3D11ShaderResourceView** ppSRVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      pBuffer->GetDesc( &descBuf );

      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      desc.BufferEx.FirstElement = 0;

      if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
      {
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
      }
      else
      {
        return E_INVALIDARG;
      }

      return pDevice->CreateShaderResourceView( pBuffer, &desc, ppSRVOut );
    }

    //--------------------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT SurfaceMesh::CreateBufferUAV( ID3D11Device* pDevice,
                                          ID3D11Buffer* pBuffer,
                                          ID3D11UnorderedAccessView** ppUAVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      pBuffer->GetDesc( &descBuf );

      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;

      if ( descBuf.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED )
      {
        desc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
        desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
      }
      else
      {
        return E_INVALIDARG;
      }

      return pDevice->CreateUnorderedAccessView( pBuffer, &desc, ppUAVOut );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseDeviceDependentResources()
    {
      // Clear out active resources.
      ReleaseVertexResources();

      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetIsActive( const bool& isActive )
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::DateTime& SurfaceMesh::GetLastUpdateTime() const
    {
      return m_lastUpdateTime;
    }

    //----------------------------------------------------------------------------
    const float& SurfaceMesh::GetLastActiveTime() const
    {
      return m_lastActiveTime;
    }

    //----------------------------------------------------------------------------
    const bool& SurfaceMesh::GetIsActive() const
    {
      return m_isActive;
    }

    //----------------------------------------------------------------------------
    concurrency::task<void> SurfaceMesh::ComputeAndStoreOBBAsync()
    {
      return concurrency::create_task( [&]()
      {

      } );

      /*
      // Compute an OBB from the list of points given. Return the corner point
      // and the three axes defining the orientation of the OBB. Also return
      // a sorted list of relative "sizes" of axes for comparison purposes.
      //void vtkOBBTree::ComputeOBB(vtkPoints *pts, double corner[3], double max[3],
      //double mid[3], double min[3], double size[3])
      int i;
      int pointId;
      double x[3], mean[3], xp[3], *v[3], v0[3], v1[3], v2[3];
      double* a[3], a0[3], a1[3], a2[3];
      double tMin[3], tMax[3], closest[3], t;

      //
      // Compute mean
      //
      int numPts = m_surfaceMesh->VertexPositions->ElementCount;
      mean[0] = mean[1] = mean[2] = 0.0;
      for ( pointId = 0; pointId < numPts; pointId++ )
      {
        pts->GetPoint( pointId, x );
        for ( i = 0; i < 3; i++ )
        {
          mean[i] += x[i];
        }
      }
      for ( i = 0; i < 3; i++ )
      {
        mean[i] /= numPts;
      }

      //
      // Compute covariance matrix
      //
      a[0] = a0;
      a[1] = a1;
      a[2] = a2;
      for ( i = 0; i < 3; i++ )
      {
        a0[i] = a1[i] = a2[i] = 0.0;
      }

      for ( pointId = 0; pointId < numPts; pointId++ )
      {
        pts->GetPoint( pointId, x );
        xp[0] = x[0] - mean[0];
        xp[1] = x[1] - mean[1];
        xp[2] = x[2] - mean[2];
        for ( i = 0; i < 3; i++ )
        {
          a0[i] += xp[0] * xp[i];
          a1[i] += xp[1] * xp[i];
          a2[i] += xp[2] * xp[i];
        }
      }//for all points

      for ( i = 0; i < 3; i++ )
      {
        a0[i] /= numPts;
        a1[i] /= numPts;
        a2[i] /= numPts;
      }

      //
      // Extract axes (i.e., eigenvectors) from covariance matrix.
      //
      v[0] = v0;
      v[1] = v1;
      v[2] = v2;
      vtkMath::Jacobi( a, size, v );
      max[0] = v[0][0];
      max[1] = v[1][0];
      max[2] = v[2][0];
      mid[0] = v[0][1];
      mid[1] = v[1][1];
      mid[2] = v[2][1];
      min[0] = v[0][2];
      min[1] = v[1][2];
      min[2] = v[2][2];

      for ( i = 0; i < 3; i++ )
      {
        a[0][i] = mean[i] + max[i];
        a[1][i] = mean[i] + mid[i];
        a[2][i] = mean[i] + min[i];
      }

      //
      // Create oriented bounding box by projecting points onto eigenvectors.
      //
      tMin[0] = tMin[1] = tMin[2] = VTK_DOUBLE_MAX;
      tMax[0] = tMax[1] = tMax[2] = -VTK_DOUBLE_MAX;

      for ( pointId = 0; pointId < numPts; pointId++ )
      {
        pts->GetPoint( pointId, x );
        for ( i = 0; i < 3; i++ )
        {
          vtkLine::DistanceToLine( x, mean, a[i], t, closest );
          if ( t < tMin[i] )
          {
            tMin[i] = t;
          }
          if ( t > tMax[i] )
          {
            tMax[i] = t;
          }
        }
      }//for all points

      for ( i = 0; i < 3; i++ )
      {
        corner[i] = mean[i] + tMin[0] * max[i] + tMin[1] * mid[i] + tMin[2] * min[i];

        max[i] = ( tMax[0] - tMin[0] ) * max[i];
        mid[i] = ( tMax[1] - tMin[1] ) * mid[i];
        min[i] = ( tMax[2] - tMin[2] ) * min[i];
      }
      */
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateConstantBuffer( ID3D11Device* device )
    {
      // Create the Const Buffer
      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory( &constant_buffer_desc, sizeof( constant_buffer_desc ) );
      constant_buffer_desc.ByteWidth = sizeof( ConstantBuffer );
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;
      auto hr = device->CreateBuffer( &constant_buffer_desc, nullptr, m_constantBuffer.GetAddressOf() );
      if ( FAILED( hr ) )
      {
        return hr;
      }

#if defined(_DEBUG) || defined(PROFILE)
      m_constantBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "ConstantBuffer" ) - 1, "ConstantBuffer" );
#endif

      return hr;
    }
  }
}