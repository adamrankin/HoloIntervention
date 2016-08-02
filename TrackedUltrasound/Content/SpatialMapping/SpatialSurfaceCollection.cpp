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

      CreateDeviceDependentResourcesAsync();
    };

    //----------------------------------------------------------------------------
    SpatialSurfaceCollection::~SpatialSurfaceCollection()
    {
      ReleaseDeviceDependentResources();
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
        float lastActiveTime = surfaceMesh->GetLastActiveTime();
        float inactiveDuration = timeElapsed - lastActiveTime;
        if ( inactiveDuration > MAX_INACTIVE_MESH_TIME_SEC )
        {
          // Surface mesh is expired.
          m_meshCollection.erase( iter++ );
          continue;
        }

        // Update the surface mesh.
        surfaceMesh->UpdateTransform( timer, coordinateSystem );

        ++iter;
      };
    }

    //----------------------------------------------------------------------------
    Concurrency::task<void> SpatialSurfaceCollection::CreateDeviceDependentResourcesAsync()
    {
      auto meshTask = create_task([&]()
      {
        std::lock_guard<std::mutex> guard(m_meshCollectionLock);

        for (auto iter = m_meshCollection.begin(); iter != m_meshCollection.end(); )
        {
          auto& pair = *iter;
          auto& surfaceMesh = pair.second;

          surfaceMesh->CreateDeviceDependentResources();
        }
      });

      auto createTask = CreateComputeShaderAsync( L"ms-appx:///CSRayTriangleIntersection.cso" );
      m_shaderLoadTask = std::make_unique< Concurrency::task<HRESULT> >( createTask );
      auto finalTask = m_shaderLoadTask->then( [&]( HRESULT hr )
      {
        if ( FAILED( hr ) )
        {
          _com_error err( hr, NULL );
          LPCTSTR errMsg = err.ErrorMessage();
          std::stringstream ss;
          ss << "Unable to load shader: " << errMsg << ".\n";
          OutputDebugStringA( ss.str().c_str() );
        }

        hr = CreateConstantBuffer();
        if ( FAILED( hr ) )
        {
          OutputDebugStringA( "Unable to load constant buffer in SpatialSurfaceCollection." );
          ReleaseDeviceDependentResources();
        }

        m_resourcesLoaded = true;
      } );

      return meshTask && finalTask;
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::ReleaseDeviceDependentResources()
    {
      m_resourcesLoaded = false;
      m_shaderLoadTask = nullptr;
      m_d3d11ComputeShader = nullptr;
      m_constantBuffer = nullptr;
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::AddSurface( Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions )
    {
      AddOrUpdateSurfaceAsync( id, newSurface, meshOptions ).then( [&]( Concurrency::task<void> previousTask )
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
      AddOrUpdateSurfaceAsync( id, newSurface, meshOptions ).then( [&]( Concurrency::task<void> previousTask )
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
    HRESULT SpatialSurfaceCollection::CreateConstantBuffer()
    {
      // Create the Const Buffer
      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory( &constant_buffer_desc, sizeof( constant_buffer_desc ) );
      constant_buffer_desc.ByteWidth = sizeof( ConstantBuffer );
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;

      auto hr = m_deviceResources->GetD3DDevice()->CreateBuffer( &constant_buffer_desc, nullptr, &m_constantBuffer );
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
      auto processMeshTask = createMeshTask.then( [this, id]( Concurrency::task<SpatialSurfaceMesh^> meshTask )
      {
        auto mesh = meshTask.get();
        if ( mesh != nullptr )
        {
          std::lock_guard<std::mutex> guard( m_meshCollectionLock );

          if (m_meshCollection.find(id) == m_meshCollection.end())
          {
            // Create a new surface and insert it
            m_meshCollection[id] = std::make_shared<SurfaceMesh>(m_deviceResources);
          }
          auto surfaceMesh = m_meshCollection[id];
          surfaceMesh->UpdateSurface( mesh );
          surfaceMesh->SetIsActive( true );
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
    bool SpatialSurfaceCollection::TestRayIntersection( uint64_t frameNumber, const float3 rayOrigin, const float3 rayDirection, float3& outHitPosition, float3& outHitNormal )
    {
      if ( !m_resourcesLoaded )
      {
        auto result = m_shaderLoadTask->get();
        if ( FAILED( result ) )
        {
          OutputDebugStringA( "Unable to load shader. Aborting.\n" );
          return false;
        }
      }

      m_deviceResources->GetD3DDeviceContext()->CSSetShader( m_d3d11ComputeShader.Get(), nullptr, 0 );

      bool result( false );
      for ( auto& pair : m_meshCollection )
      {
        pair.second->SetRayConstants( *m_deviceResources->GetD3DDeviceContext(), m_constantBuffer.Get(), rayOrigin, rayDirection );

        if ( pair.second->TestRayIntersection( *m_deviceResources->GetD3DDevice(), *m_deviceResources->GetD3DDeviceContext(), *m_d3d11ComputeShader.Get(), frameNumber, outHitPosition, outHitNormal ) )
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

        surfaceMesh->SetIsActive( surfaceCollection->HasKey( id ) ? true : false );
      };
    }

    //--------------------------------------------------------------------------------------
    Concurrency::task<HRESULT> SpatialSurfaceCollection::CreateComputeShaderAsync( const std::wstring& srcFile )
    {
      Concurrency::task<std::vector<byte>> loadVSTask = DX::ReadDataAsync( srcFile );
      return loadVSTask.then( [ = ]( std::vector<byte> data )
      {
        auto hr = m_deviceResources->GetD3DDevice()->CreateComputeShader( &data.front(), data.size(), nullptr, &m_d3d11ComputeShader );

        if ( FAILED( hr ) )
        {
          return hr;
        }

#if defined(_DEBUG) || defined(PROFILE)
        if ( SUCCEEDED( hr ) )
        {
          m_d3d11ComputeShader->SetPrivateData( WKPDID_D3DDebugObjectName, strlen( "main" ) - 1, "main" );
        }
#endif

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
        return mesh->GetLastUpdateTime();
      }
      else
      {
        static const Windows::Foundation::DateTime zero;
        return zero;
      }
    }
  }
}