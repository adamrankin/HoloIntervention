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

#pragma once

// Local includes
#include "SurfaceMesh.h"
#include "StepTimer.h"

// STL includes
#include <memory>
#include <map>

// WinRT includes
#include <ppltasks.h>

namespace TrackedUltrasound
{
  namespace Spatial
  {
    class SpatialSurfaceCollection
    {
    public:
      SpatialSurfaceCollection( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      void Update( DX::StepTimer const& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem );

      bool HasSurface( Platform::Guid id );
      void AddSurface( Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface );
      void UpdateSurface( Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface );
      void RemoveSurface( Platform::Guid id );
      void ClearSurfaces();

      Windows::Foundation::DateTime GetLastUpdateTime( Platform::Guid id );

      void HideInactiveMeshes(
        Windows::Foundation::Collections::IMapView<Platform::Guid,
        Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^>^ const& surfaceCollection );

    private:
      Concurrency::task<void> AddOrUpdateSurfaceAsync( Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface );

      // The set of surfaces in the collection.
      std::map<Platform::Guid, SurfaceMesh>           m_meshCollection;

      // A way to lock map access.
      std::mutex                                      m_meshCollectionLock;

      // Total number of surface meshes.
      unsigned int                                    m_surfaceMeshCount;

      // Level of detail setting. The number of triangles that the system is allowed to provide per cubic meter.
      double                                          m_maxTrianglesPerCubicMeter = 1000.0;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;

      // The duration of time, in seconds, a mesh is allowed to remain inactive before deletion.
      const float c_maxInactiveMeshTime = 120.f;
    };
  }
}