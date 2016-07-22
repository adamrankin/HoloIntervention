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
      });

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