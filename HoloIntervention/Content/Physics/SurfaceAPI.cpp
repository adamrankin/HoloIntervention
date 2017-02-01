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

// Local includes
#include "pch.h"
#include "AppView.h"
#include "SurfaceAPI.h"

// Common includes
#include "DeviceResources.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <agents.h>
#include <functional>
#include <ppltasks.h>
#include <sstream>

// Spatial includes
#include "SurfaceMesh.h"

// Rendering includes
#include "ModelRenderer.h"
#include "ModelEntry.h"

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Physics
  {
    const uint32 SurfaceAPI::INIT_SURFACE_RETRY_DELAY_MS = 100;

    //----------------------------------------------------------------------------
    SurfaceAPI::SurfaceAPI(System::NotificationSystem& notificationSystem, const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer)
      : m_deviceResources(deviceResources)
      , m_notificationSystem(notificationSystem)
      , m_stepTimer(stepTimer)
    {
      m_surfaceCollection = std::make_unique<Spatial::SpatialSurfaceCollection>(m_notificationSystem, m_deviceResources, stepTimer);
    }

    //----------------------------------------------------------------------------
    SurfaceAPI::~SurfaceAPI()
    {
      if (m_surfaceObserver != nullptr)
      {
        m_surfaceObserver->ObservedSurfacesChanged -= m_surfaceObserverEventToken;
      }

      m_surfaceCollection = nullptr;
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      // Keep the surface observer positioned at the device's location.
      UpdateSurfaceObserverPosition(coordinateSystem);

      m_surfaceCollection->Update(coordinateSystem);
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::CreateDeviceDependentResources()
    {
      try
      {
        m_surfaceCollection->CreateDeviceDependentResourcesAsync().then([this]()
        {
          m_componentReady = true;
        });
      }
      catch (const std::exception&)
      {
        m_notificationSystem.QueueMessage("Unable to start spatial system.");
      }
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;
      m_surfaceCollection->ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::OnSurfacesChanged(SpatialSurfaceObserver^ sender, Object^ args)
    {
      IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection = sender->GetObservedSurfaces();

      // Process surface adds and updates.
      for (const auto& pair : surfaceCollection)
      {
        auto id = pair->Key;
        auto surfaceInfo = pair->Value;

        if (m_surfaceCollection->HasSurface(id))
        {
          if (m_surfaceCollection->GetLastUpdateTime(id).UniversalTime < surfaceInfo->UpdateTime.UniversalTime)
          {
            // Update existing surface.
            m_surfaceCollection->AddOrUpdateSurfaceAsync(id, surfaceInfo, m_surfaceMeshOptions);
          }
        }
        else
        {
          // New surface.
          m_surfaceCollection->AddOrUpdateSurfaceAsync(id, surfaceInfo, m_surfaceMeshOptions);
        }
      }

      m_surfaceCollection->HideInactiveMeshes(surfaceCollection);
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::UpdateSurfaceObserverPosition(SpatialCoordinateSystem^ coordinateSystem)
    {
      // 20 meters wide, and 5 meters tall, centered at the origin of coordinateSystem.
      SpatialBoundingBox aabb =
      {
        { 0.f,  0.f, 0.f },
        { 20.f, 20.f, 5.f },
      };

      if (m_surfaceObserver != nullptr)
      {
        SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromBox(coordinateSystem, aabb);
        m_surfaceObserver->SetBoundingVolume(bounds);
      }
    }

    //----------------------------------------------------------------------------
    bool SurfaceAPI::TestRayIntersection(SpatialCoordinateSystem^ desiredCoordinateSystem, const float3 rayOrigin, const float3 rayDirection, float3& outHitPosition, float3& outHitNormal, float3& outHitEdge)
    {
      return m_surfaceCollection->TestRayIntersection(desiredCoordinateSystem, rayOrigin, rayDirection, outHitPosition, outHitNormal, outHitEdge);
    }

    //----------------------------------------------------------------------------
    bool SurfaceAPI::GetLastHitPosition(_Out_ float3& position, _In_ bool considerOldHits /*= false*/)
    {
      if (m_surfaceCollection == nullptr)
      {
        return false;
      }
      return m_surfaceCollection->GetLastHitPosition(position, considerOldHits);
    }

    //----------------------------------------------------------------------------
    bool SurfaceAPI::GetLastHitNormal(_Out_ float3& normal, _In_ bool considerOldHits /*= false*/)
    {
      if (m_surfaceCollection == nullptr)
      {
        return false;
      }
      return m_surfaceCollection->GetLastHitNormal(normal, considerOldHits);
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Spatial::SurfaceMesh> SurfaceAPI::GetLastHitMesh()
    {
      return m_surfaceCollection->GetLastHitMesh();
    }

    //----------------------------------------------------------------------------
    Platform::Guid SurfaceAPI::GetLastHitMeshGuid()
    {
      return m_surfaceCollection->GetLastHitMeshGuid();
    }

    //----------------------------------------------------------------------------
    task<bool> SurfaceAPI::InitializeSurfaceObserverAsync(SpatialCoordinateSystem^ coordinateSystem)
    {
      if (m_surfaceObserver != nullptr)
      {
        m_surfaceObserver->ObservedSurfacesChanged -= m_surfaceObserverEventToken;
        m_surfaceObserver = nullptr;
      }

      auto initSurfaceObserverTask = create_task(SpatialSurfaceObserver::RequestAccessAsync());
      return initSurfaceObserverTask.then([this, coordinateSystem](Windows::Perception::Spatial::SpatialPerceptionAccessStatus status)
      {
        switch (status)
        {
        case SpatialPerceptionAccessStatus::Allowed:
        {
          m_surfaceMeshOptions = ref new SpatialSurfaceMeshOptions();

          IVectorView<DirectXPixelFormat>^ supportedVertexPositionFormats = m_surfaceMeshOptions->SupportedVertexPositionFormats;
          unsigned int formatIndex = 0;
          if (supportedVertexPositionFormats->IndexOf(DirectXPixelFormat::R32G32B32Float, &formatIndex))
          {
            m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R32G32B32Float;
          }
          else if (supportedVertexPositionFormats->IndexOf(DirectXPixelFormat::R32G32B32A32Float, &formatIndex))
          {
            m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R32G32B32A32Float;
          }
          else
          {
            HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, "Cannot load desired vertex position format.");
          }

          // Our shader pipeline can handle a variety of triangle index formats
          IVectorView<DirectXPixelFormat>^ supportedTriangleIndexFormats = m_surfaceMeshOptions->SupportedTriangleIndexFormats;
          if (supportedTriangleIndexFormats->IndexOf(DirectXPixelFormat::R32UInt, &formatIndex))
          {
            m_surfaceMeshOptions->TriangleIndexFormat = DirectXPixelFormat::R32UInt;
          }
          else
          {
            HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, "Cannot load desired index format.");
          }

          if (m_surfaceObserver == nullptr)
          {
            m_surfaceObserver = ref new SpatialSurfaceObserver();
            UpdateSurfaceObserverPosition(coordinateSystem);
          }
        }
        break;
        case SpatialPerceptionAccessStatus::DeniedBySystem:
        {
          HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Error: Cannot initialize surface observer because the system denied access to the spatialPerception capability.");
        }
        break;
        case SpatialPerceptionAccessStatus::DeniedByUser:
        {
          HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Error: Cannot initialize surface observer because the user denied access to the spatialPerception capability.");
        }
        break;
        case SpatialPerceptionAccessStatus::Unspecified:
        {
          HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Error: Cannot initialize surface observer. Access was denied for an unspecified reason.");
        }
        break;
        default:
          // unreachable
          assert(false);
          break;
        }

        if (m_surfaceObserver != nullptr)
        {
          return create_task([this]() -> bool
          {
            uint32 msCount(0);
            while (m_surfaceObserver->GetObservedSurfaces()->Size == 0 && msCount < 5000)
            {
              msCount += 100;
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            if (msCount >= 5000)
            {
              return false;
            }

            for (auto const& pair : m_surfaceObserver->GetObservedSurfaces())
            {
              auto const& id = pair->Key;
              auto const& surfaceInfo = pair->Value;
              m_surfaceCollection->AddOrUpdateSurfaceAsync(id, surfaceInfo, m_surfaceMeshOptions);
            }

            m_surfaceObserverEventToken = m_surfaceObserver->ObservedSurfacesChanged +=
            ref new Windows::Foundation::TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(std::bind(&SurfaceAPI::OnSurfacesChanged, this, std::placeholders::_1, std::placeholders::_2));

            CreateDeviceDependentResources();

            m_componentReady = true;

            return true;
          });
        }

        return task_from_result<bool>(false);
      });
    }

    //----------------------------------------------------------------------------
    task<void> SurfaceAPI::SaveAppStateAsync()
    {
      return task<SpatialAnchorStore^>(SpatialAnchorManager::RequestStoreAsync()).then([ = ](SpatialAnchorStore ^ store)
      {
        std::lock_guard<std::mutex> guard(m_anchorMutex);
        if (store == nullptr)
        {
          return;
        }

        for (auto pair : m_spatialAnchors)
        {
          if (!store->TrySave(pair.first, pair.second))
          {
            m_notificationSystem.QueueMessage(L"Unable to save spatial anchor " + pair.first);
          }
        }
      });
    }

    //----------------------------------------------------------------------------
    task<void> SurfaceAPI::LoadAppStateAsync()
    {
      m_spatialAnchors.clear();

      return task<SpatialAnchorStore^>(SpatialAnchorManager::RequestStoreAsync()).then([ = ](SpatialAnchorStore ^ store)
      {
        std::lock_guard<std::mutex> guard(m_anchorMutex);
        if (store == nullptr)
        {
          return;
        }

        int i(0);
        Windows::Foundation::Collections::IMapView<Platform::String^, SpatialAnchor^>^ output = store->GetAllSavedAnchors();
        for (auto pair : output)
        {
          m_spatialAnchors[pair->Key] = pair->Value;
        }
      });
    }

    //----------------------------------------------------------------------------
    bool SurfaceAPI::DropAnchorAtIntersectionHit(Platform::String^ anchorName, SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose)
    {
      if (anchorName == nullptr)
      {
        m_notificationSystem.QueueMessage(L"Unable to create anchor. No name specified.");
        return false;
      }

      float3 outHitPosition;
      float3 outHitNormal;
      float3 outHitEdge;
      bool hit = TestRayIntersection(coordinateSystem,
                                     headPose->Head->Position,
                                     headPose->Head->ForwardDirection,
                                     outHitPosition,
                                     outHitNormal,
                                     outHitEdge);

      if (!hit)
      {
        m_notificationSystem.QueueMessage(L"Unable to compute mesh intersection hit.");
        return false;
      }

      float4x4 anchorMatrix = make_float4x4_world(outHitPosition, outHitEdge, -outHitNormal);
      quaternion rotation;
      float3 translation;
      float3 scale;
      if (!decompose(anchorMatrix, &scale, &rotation, &translation))
      {
        m_notificationSystem.QueueMessage(L"Unable to determine coordinate system of anchor. Please try again.");
        return false;
      }
      SpatialAnchor^ anchor = SpatialAnchor::TryCreateRelativeTo(coordinateSystem, translation, rotation);

      if (anchor == nullptr)
      {
        m_notificationSystem.QueueMessage(L"Unable to create anchor.");
        return false;
      }

      std::lock_guard<std::mutex> lock(m_anchorMutex);
      m_spatialAnchors[anchorName] = anchor;

      m_notificationSystem.QueueMessage(L"Anchor " + anchorName + L" created.");

      return true;
    }

    //----------------------------------------------------------------------------
    size_t SurfaceAPI::RemoveAnchor(Platform::String^ name)
    {
      std::lock_guard<std::mutex> lock(m_anchorMutex);
      return m_spatialAnchors.erase(name);
    }

    //----------------------------------------------------------------------------
    SpatialAnchor^ SurfaceAPI::GetAnchor(Platform::String^ anchorName)
    {
      if (HasAnchor(anchorName))
      {
        return m_spatialAnchors[anchorName];
      }

      return nullptr;
    }

    //----------------------------------------------------------------------------
    bool SurfaceAPI::HasAnchor(Platform::String^ anchorName)
    {
      return m_spatialAnchors.find(anchorName) != m_spatialAnchors.end();
    }

    //----------------------------------------------------------------------------
    void SurfaceAPI::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {

    }
  }
}