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

#include "IVoiceInput.h"
#include "SpatialSurfaceCollection.h"

// WinRT includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace DX
{
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
    class ModelEntry;
  }

  namespace System
  {
    class SpatialSystem : public Sound::IVoiceInput
    {
    public:
      SpatialSystem(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer);
      ~SpatialSystem();

      void Update(SpatialCoordinateSystem^ coordinateSystem);

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Handle surface change events.
      void OnSurfacesChanged(SpatialSurfaceObserver^ sender, Platform::Object^ args);

      // Positions the Spatial Mapping surface observer at the origin of the given coordinate system.
      void UpdateSurfaceObserverPosition(SpatialCoordinateSystem^ coordinateSystem);

      // Perform a ray cast to determine if the ray hits any stored mesh
      bool TestRayIntersection(SpatialCoordinateSystem^ desiredCoordinateSystem,
                               const float3 rayOrigin,
                               const float3 rayDirection,
                               float3& outHitPosition,
                               float3& outHitNormal,
                               float3& outHitEdge);
      bool GetLastHitPosition(_Out_ float3& position, _In_ bool considerOldHits = false);
      bool GetLastHitNormal(_Out_ float3& normal, _In_ bool considerOldHits = false);
      std::shared_ptr<Spatial::SurfaceMesh> GetLastHitMesh();

      // Initializes the Spatial Mapping surface observer.
      void InitializeSurfaceObserver(SpatialCoordinateSystem^ coordinateSystem);

      // Handle saving and loading of app state owned by AppMain.
      task<void> SaveAppStateAsync();
      task<void> LoadAppStateAsync();

      bool DropAnchorAtIntersectionHit(Platform::String^ anchorName, SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose);
      size_t RemoveAnchor(Platform::String^ anchorName);
      SpatialAnchor^ GetAnchor(Platform::String^ anchorName);
      bool HasAnchor(Platform::String^ anchorName);

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      void OnRawCoordinateSystemAdjusted(SpatialAnchor^ sender, SpatialAnchorRawCoordinateSystemAdjustedEventArgs^ args);

    protected:
      // Event registration tokens.
      Windows::Foundation::EventRegistrationToken                       m_surfaceObserverEventToken;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>                              m_deviceResources;
      DX::StepTimer&                                                    m_stepTimer;

      // Anchor interaction variables
      std::mutex                                                        m_anchorMutex;

      // Obtains spatial mapping data from the device in real time.
      SpatialSurfaceObserver^                                           m_surfaceObserver;
      SpatialSurfaceMeshOptions^                                        m_surfaceMeshOptions;

      // A data handler for surface meshes.
      std::unique_ptr<Spatial::SpatialSurfaceCollection>                m_surfaceCollection;

      // List of spatial anchors
      std::map<Platform::String^, SpatialAnchor^>                       m_spatialAnchors;

      static const uint32                                               INIT_SURFACE_RETRY_DELAY_MS;
    };
  }
}