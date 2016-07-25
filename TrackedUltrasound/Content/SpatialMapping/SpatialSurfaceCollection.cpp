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
#include "SpatialShaderStructures.h"
#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"

// Windows includes
#include <comdef.h>

// STL includes
#include <sstream>

using namespace Concurrency;
using namespace DX;
using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

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
      m_shaderLoadTask->then( [&]( concurrency::task<HRESULT> previousTask )
      {
        try
        {
          auto hr = previousTask.get();
          if ( FAILED( hr ) )
          {
            _com_error err( hr, NULL );
            LPCTSTR errMsg = err.ErrorMessage();
            std::stringstream ss;
            ss << "Unable to load shader: " << errMsg << ".\n";
            OutputDebugStringA( ss.str().c_str() );
          }
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
    };

    //----------------------------------------------------------------------------
    SpatialSurfaceCollection::~SpatialSurfaceCollection()
    {
      SAFE_RELEASE( m_constantBuffer );
      SAFE_RELEASE( m_d3d11ComputeShader );
    }

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

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::AddSurface( Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions )
    {
      AddOrUpdateSurfaceAsync( id, newSurface, meshOptions ).then( [&]( concurrency::task<void> previousTask )
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
    void SpatialSurfaceCollection::UpdateSurface( Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions )
    {
      AddOrUpdateSurfaceAsync( id, newSurface, meshOptions ).then( [&]( concurrency::task<void> previousTask )
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
    HRESULT SpatialSurfaceCollection::CreateConstantBuffer( ID3D11Device* device )
    {
      // Create the Const Buffer
      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory( &constant_buffer_desc, sizeof( constant_buffer_desc ) );
      constant_buffer_desc.ByteWidth = sizeof( ConstantBuffer );
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;

      auto hr = device->CreateBuffer( &constant_buffer_desc, nullptr, &m_constantBuffer );
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
    Concurrency::task<void> SpatialSurfaceCollection::AddOrUpdateSurfaceAsync( Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions )
    {
      // The level of detail setting is used to limit mesh complexity, by limiting the number
      // of triangles per cubic meter.
      auto createMeshTask = create_task( newSurface->TryComputeLatestMeshAsync( m_maxTrianglesPerCubicMeter, meshOptions ) );
      auto processMeshTask = createMeshTask.then( [this, id]( concurrency::task<SpatialSurfaceMesh^> meshTask )
      {
        auto mesh = meshTask.get();
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
    bool SpatialSurfaceCollection::TestRayIntersection( uint64_t frameNumber, const float3 rayOrigin, const float3 rayDirection, std::vector<float>& outHitPosition, std::vector<float>& outHitNormal )
    {
      if ( !m_shaderLoaded )
      {
        auto result = m_shaderLoadTask->get();
        if ( FAILED( result ) )
        {
          OutputDebugStringA( "Unable to load shader. Aborting.\n" );
          return false;
        }
      }

      m_deviceResources->GetD3DDeviceContext()->CSSetShader( m_d3d11ComputeShader, nullptr, 0 );

      bool result( false );
      for ( auto& pair : m_meshCollection )
      {
        pair.second.SetRayConstants( m_deviceResources->GetD3DDeviceContext(), m_constantBuffer, rayOrigin, rayDirection );

        if ( pair.second.TestRayIntersection( *m_deviceResources->GetD3DDevice(), *m_deviceResources->GetD3DDeviceContext(), *m_d3d11ComputeShader, frameNumber, outHitPosition, outHitNormal ) )
        {
          result = true;
          break;
        }
      }

      m_deviceResources->GetD3DDeviceContext()->CSSetShader( nullptr, nullptr, 0 );

      return result;
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

        auto hr = pDevice->CreateComputeShader( &data.front(), data.size(), nullptr, ppShaderOut );

        if ( FAILED( hr ) )
        {
          throw std::exception( "Unable to create compute shader. Aborting." );
        }

#if defined(_DEBUG) || defined(PROFILE)
        if ( SUCCEEDED( hr ) )
        {
          ( *ppShaderOut )->SetPrivateData( WKPDID_D3DDebugObjectName, strlen( "main" ) - 1, "main" );
        }
#endif

        hr = CreateConstantBuffer( pDevice );

        if ( FAILED( hr ) || m_constantBuffer == NULL )
        {
          throw std::exception( "Unable to create constant buffer. Aborting." );
        }

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