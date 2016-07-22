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
#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"

using namespace Concurrency;
using namespace DX;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Platform;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SpatialSurfaceCollection::SpatialSurfaceCollection( const std::shared_ptr<DX::DeviceResources>& deviceResources ) :
      m_deviceResources( deviceResources )
    {
      m_meshCollection.clear();

      auto createTask = CreateComputeShaderAsync( L"ms-appx:///CSRayTriangleIntersection.cso", deviceResources->GetD3DDevice(), &m_d3d11ComputeShader );
      m_shaderLoadTask = std::make_unique< concurrency::task<HRESULT> >( createTask );
    };

    //----------------------------------------------------------------------------
    // Called once per frame, maintains and updates the mesh collection.
    void SpatialSurfaceCollection::Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem )
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );

      const float timeElapsed = static_cast<float>( timer.GetTotalSeconds() );

      // Update meshes as needed, based on the current coordinate system.
      // Also remove meshes that are inactive for too long.
      for ( auto iter = m_meshCollection.begin(); iter != m_meshCollection.end(); )
      {
        auto& pair = *iter;
        auto& surfaceMesh = pair.second;

        // Check to see if the mesh has expired.
        float lastActiveTime = surfaceMesh.GetLastActiveTime();
        float inactiveDuration = timeElapsed - lastActiveTime;
        if ( inactiveDuration > c_maxInactiveMeshTime )
        {
          // Surface mesh is expired.
          m_meshCollection.erase( iter++ );
          continue;
        }

        // Update the surface mesh.
        surfaceMesh.UpdateTransform( m_deviceResources->GetD3DDeviceContext(), timer, coordinateSystem );

        ++iter;
      };
    }

    //--------------------------------------------------------------------------------------
    void SpatialSurfaceCollection::SetConstants( ID3D11DeviceContext* context, const std::vector<double>& rayOrigin, const std::vector<double>& rayDirection )
    {
      ConstantBuffer cb;
      context->UpdateSubresource( m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0 );
      context->CSSetConstantBuffers( 0, 1, &m_constantBuffer );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::AddSurface( Guid id, SpatialSurfaceInfo^ newSurface )
    {
      AddOrUpdateSurfaceAsync( id, newSurface );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::UpdateSurface( Guid id, SpatialSurfaceInfo^ newSurface )
    {
      AddOrUpdateSurfaceAsync( id, newSurface );
    }

    //----------------------------------------------------------------------------
    HRESULT SpatialSurfaceCollection::CreateConstantBuffer( ID3D11Device* device )
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

    //----------------------------------------------------------------------------
    Concurrency::task<void> SpatialSurfaceCollection::AddOrUpdateSurfaceAsync( Guid id, SpatialSurfaceInfo^ newSurface )
    {
      auto options = ref new SpatialSurfaceMeshOptions();
      options->IncludeVertexNormals = true;

      // The level of detail setting is used to limit mesh complexity, by limiting the number
      // of triangles per cubic meter.
      auto createMeshTask = create_task( newSurface->TryComputeLatestMeshAsync( m_maxTrianglesPerCubicMeter, options ) );
      auto processMeshTask = createMeshTask.then( [this, id]( SpatialSurfaceMesh ^ mesh )
      {
        if ( mesh != nullptr )
        {
          std::lock_guard<std::mutex> guard( m_meshCollectionLock );

          auto& surfaceMesh = m_meshCollection[id];
          surfaceMesh.UpdateSurface( mesh, m_deviceResources->GetD3DDevice() );
          surfaceMesh.SetIsActive( true );
        }
      } );

      return processMeshTask;
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::RemoveSurface( Guid id )
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );
      m_meshCollection.erase( id );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::ClearSurfaces()
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );
      m_meshCollection.clear();
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::TestRayIntersection( const DX::StepTimer& timer, const std::vector<double>& origin, const std::vector<double>& direction, std::vector<double>& outResult )
    {
      if ( !m_shaderLoaded )
      {
        auto result = m_shaderLoadTask->get();
        if ( FAILED( result ) )
        {
          OutputDebugStringA( "Unable to load shader. Aborting." );
          return false;
        }
      }

      m_deviceResources->GetD3DDeviceContext()->CSSetShader( m_d3d11ComputeShader.Get(), nullptr, 0 );

      SetConstants( m_deviceResources->GetD3DDeviceContext(), origin, direction );

      // TODO : implement pre-check using OBB
      for ( auto& pair : m_meshCollection )
      {
        if ( pair.second.TestRayIntersection( *m_deviceResources->GetD3DDeviceContext(), *( m_d3d11ComputeShader.Get() ), timer, outResult ) )
        {
          return true;
        }
      }

      m_deviceResources->GetD3DDeviceContext()->CSSetShader( nullptr, nullptr, 0 );

      ID3D11Buffer* ppCBnullptr[1] = { nullptr };
      m_deviceResources->GetD3DDeviceContext()->CSSetConstantBuffers( 0, 1, ppCBnullptr );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::HideInactiveMeshes( IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection )
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );

      // Hide surfaces that aren't actively listed in the surface collection.
      for ( auto& pair : m_meshCollection )
      {
        const auto& id = pair.first;
        auto& surfaceMesh = pair.second;

        surfaceMesh.SetIsActive( surfaceCollection->HasKey( id ) ? true : false );
      };
    }

    //--------------------------------------------------------------------------------------
    concurrency::task<HRESULT> SpatialSurfaceCollection::CreateComputeShaderAsync( const std::wstring& srcFile,
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
          ( *ppShaderOut )->SetPrivateData( WKPDID_D3DDebugObjectName, strlen( "main" ) - 1, "main" );
        }

#endif

        m_shaderLoaded = true;

        return hr;
      } );
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::HasSurface( Platform::Guid id )
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );
      return m_meshCollection.find( id ) != m_meshCollection.end();
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::DateTime SpatialSurfaceCollection::GetLastUpdateTime( Platform::Guid id )
    {
      std::lock_guard<std::mutex> guard( m_meshCollectionLock );
      auto& meshIter = m_meshCollection.find( id );
      if ( meshIter != m_meshCollection.end() )
      {
        auto const& mesh = meshIter->second;
        return mesh.GetLastUpdateTime();
      }
      else
      {
        static const Windows::Foundation::DateTime zero;
        return zero;
      }
    }
  }
}