#pragma once

#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"

using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    class SpatialSurfaceAPI
    {
    public:
      SpatialSurfaceAPI( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SpatialSurfaceAPI();

      void Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem );

      // Handle surface change events.
      void OnSurfacesChanged( SpatialSurfaceObserver^ sender, Platform::Object^ args );

      // Positions the Spatial Mapping surface observer at the origin of the given coordinate system.
      void UpdateSurfaceObserverPosition( SpatialCoordinateSystem^ coordinateSystem );

      bool TestRayIntersection( const float3 rayOrigin,
                                const float3 rayDirection,
                                std::vector<float>& outHitPosition,
                                std::vector<float>& outHitNormal );

      // Initializes the Spatial Mapping surface observer.
      void InitializeSurfaceObserver( SpatialCoordinateSystem^ coordinateSystem );

      // Handle saving and loading of app state owned by AppMain.
      void SaveAppState();
      void LoadAppState();

    private:
      // Cached value of the current frame number
      uint64 m_FrameNumber;

      // Event registration tokens.
      Windows::Foundation::EventRegistrationToken m_surfaceObserverEventToken;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources> m_deviceResources;

      // Obtains spatial mapping data from the device in real time.
      SpatialSurfaceObserver^ m_surfaceObserver;
      SpatialSurfaceMeshOptions^ m_surfaceMeshOptions;

      // A data handler for surface meshes.
      std::unique_ptr<SpatialSurfaceCollection> m_surfaceCollection;

      // List of spatial anchors
      Platform::Collections::Map<Platform::String^, SpatialAnchor^>^ m_spatialAnchors;
    };
  }
}