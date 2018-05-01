/*====================================================================
Copyright(c) 2018 Adam Rankin


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

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"
#include "SpatialSurfaceCollection.h"

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Spatial
  {
    class SurfaceMesh;
  }

  namespace Rendering
  {
    class Model;
  }

  namespace Physics
  {
    class PhysicsAPI : public Input::IVoiceInput, public IEngineComponent
    {
    public:
      PhysicsAPI(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer);
      ~PhysicsAPI();

      void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

      Concurrency::task<bool> InitializeSurfaceObserverAsync(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

      Concurrency::task<bool> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();

      /// Perform a ray cast to determine if the ray hits any stored mesh
      bool TestRayIntersection(Windows::Perception::Spatial::SpatialCoordinateSystem^ desiredCoordinateSystem,
                               const Windows::Foundation::Numerics::float3 rayOrigin,
                               const Windows::Foundation::Numerics::float3 rayDirection,
                               Windows::Foundation::Numerics::float3& outHitPosition,
                               Windows::Foundation::Numerics::float3& outHitNormal,
                               Windows::Foundation::Numerics::float3& outHitEdge);
      bool GetLastHitPosition(_Out_ Windows::Foundation::Numerics::float3& position, _In_ bool considerOldHits = false);
      bool GetLastHitNormal(_Out_ Windows::Foundation::Numerics::float3& normal, _In_ bool considerOldHits = false);
      std::shared_ptr<Spatial::SurfaceMesh> GetLastHitMesh();
      Platform::Guid GetLastHitMeshGuid();
      Spatial::SpatialSurfaceCollection::GuidMeshMap GetMeshes() const;
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions^ GetMeshOptions();

      Concurrency::task<bool> SaveAppStateAsync();
      Concurrency::task<bool> LoadAppStateAsync();

      bool DropAnchorAtIntersectionHit(Platform::String^ anchorName, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);
      size_t RemoveAnchor(Platform::String^ anchorName);
      void AddOrUpdateAnchor(Windows::Perception::Spatial::SpatialAnchor^ anchor, Platform::String^ anchorName);
      Windows::Perception::Spatial::SpatialAnchor^ GetAnchor(Platform::String^ anchorName);
      bool HasAnchor(Platform::String^ anchorName);

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Input::VoiceInputCallbackMap& callbackMap);

    protected:
      /// Positions the Spatial Mapping surface observer at the origin of the given coordinate system.
      void UpdateSurfaceObserverPosition(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

      /// Handle surface change events.
      void OnSurfacesChanged(Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^ sender, Platform::Object^ args);

    protected:
      // Event registration tokens.
      Windows::Foundation::EventRegistrationToken                               m_surfaceObserverEventToken;

      // Cached entries
      std::shared_ptr<DX::DeviceResources>                                      m_deviceResources;
      DX::StepTimer&                                                            m_stepTimer;

      // Anchor interaction variables
      std::mutex                                                                m_anchorMutex;

      // Obtains spatial mapping data from the device in real time.
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^           m_surfaceObserver;
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions^        m_surfaceMeshOptions;

      // A data handler for surface meshes.
      std::unique_ptr<Spatial::SpatialSurfaceCollection>                        m_surfaceCollection;

      // List of spatial anchors
      std::map<Platform::String^, Windows::Perception::Spatial::SpatialAnchor^> m_spatialAnchors;

      static const uint32                                                       INIT_SURFACE_RETRY_DELAY_MS;
    };
  }
}