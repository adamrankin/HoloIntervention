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

#pragma once

#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"

using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace Spatial
  {
    class SpatialSystem
    {
    public:
      SpatialSystem( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SpatialSystem();

      void Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem );

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Handle surface change events.
      void OnSurfacesChanged( SpatialSurfaceObserver^ sender, Platform::Object^ args );

      // Positions the Spatial Mapping surface observer at the origin of the given coordinate system.
      void UpdateSurfaceObserverPosition( SpatialCoordinateSystem^ coordinateSystem );

      // Perform a ray cast to determine if the ray hits any stored mesh
      bool TestRayIntersection( SpatialCoordinateSystem^ desiredCoordinateSystem,
                                const float3 rayOrigin,
                                const float3 rayDirection,
                                float3& outHitPosition,
                                float3& outHitNormal );

      // Initializes the Spatial Mapping surface observer.
      void InitializeSurfaceObserver( SpatialCoordinateSystem^ coordinateSystem );

      // Handle saving and loading of app state owned by AppMain.
      void SaveAppState();
      void LoadAppState();

    private:
      // Cached value of the current frame number
      uint64 m_FrameNumber;

      // Event registration tokens.
      Windows::Foundation::EventRegistrationToken                       m_surfaceObserverEventToken;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>                              m_deviceResources;

      // Obtains spatial mapping data from the device in real time.
      SpatialSurfaceObserver^                                           m_surfaceObserver;
      SpatialSurfaceMeshOptions^                                        m_surfaceMeshOptions;

      // A data handler for surface meshes.
      std::unique_ptr<SpatialSurfaceCollection>                         m_surfaceCollection;

      // List of spatial anchors
      Platform::Collections::Map<Platform::String^, SpatialAnchor^>^    m_spatialAnchors;

      const uint32                                                      INIT_SURFACE_RETRY_DELAY_MS = 100;
    };
  }
}