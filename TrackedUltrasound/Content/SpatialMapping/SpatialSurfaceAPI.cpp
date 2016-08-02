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
#include "SpatialSurfaceAPI.h"
#include "StepTimer.h"

// WinRT includes
#include <agents.h>
#include <functional>
#include <ppltasks.h>
#include <sstream>

using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Concurrency;

namespace TrackedUltrasound
{
  namespace Spatial
  {

    //----------------------------------------------------------------------------
    SpatialSurfaceAPI::SpatialSurfaceAPI( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      m_surfaceCollection = std::make_unique<SpatialSurfaceCollection>( m_deviceResources );
    }

    //----------------------------------------------------------------------------
    SpatialSurfaceAPI::~SpatialSurfaceAPI()
    {
      if ( m_surfaceObserver != nullptr )
      {
        m_surfaceObserver->ObservedSurfacesChanged -= m_surfaceObserverEventToken;
      }
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem )
    {
      // Cache the current frame number
      m_FrameNumber = timer.GetFrameCount();

      // Keep the surface observer positioned at the device's location.
      UpdateSurfaceObserverPosition( coordinateSystem );

      m_surfaceCollection->Update( timer, coordinateSystem );
    }

    //----------------------------------------------------------------------------
    Concurrency::task<void> SpatialSurfaceAPI::CreateDeviceDependentResourcesAsync()
    {
      return m_surfaceCollection->CreateDeviceDependentResourcesAsync();
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::ReleaseDeviceDependentResources()
    {
      m_surfaceCollection->ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::OnSurfacesChanged( SpatialSurfaceObserver^ sender, Object^ args )
    {
      IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection = sender->GetObservedSurfaces();

      // Process surface adds and updates.
      for ( const auto& pair : surfaceCollection )
      {
        auto id = pair->Key;
        auto surfaceInfo = pair->Value;

        if ( m_surfaceCollection->HasSurface( id ) )
        {
          if ( m_surfaceCollection->GetLastUpdateTime( id ).UniversalTime < surfaceInfo->UpdateTime.UniversalTime )
          {
            // Update existing surface.
            m_surfaceCollection->UpdateSurface( id, surfaceInfo, m_surfaceMeshOptions );
          }
        }
        else
        {
          // New surface.
          m_surfaceCollection->AddSurface( id, surfaceInfo, m_surfaceMeshOptions );
        }
      }

      m_surfaceCollection->HideInactiveMeshes( surfaceCollection );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::UpdateSurfaceObserverPosition( SpatialCoordinateSystem^ coordinateSystem )
    {
      // 20 meters wide, and 5 meters tall, centered at the origin of coordinateSystem.
      SpatialBoundingBox aabb =
      {
        { 0.f,  0.f, 0.f },
        { 20.f, 20.f, 5.f },
      };

      if ( m_surfaceObserver != nullptr )
      {
        SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromBox( coordinateSystem, aabb );
        m_surfaceObserver->SetBoundingVolume( bounds );
      }
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceAPI::TestRayIntersection(const float3 rayOrigin, const float3 rayDirection, float3& outHitPosition, float3& outHitNormal)
    {
      return m_surfaceCollection->TestRayIntersection(m_FrameNumber, rayOrigin, rayDirection, outHitPosition, outHitNormal);
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::InitializeSurfaceObserver( SpatialCoordinateSystem^ coordinateSystem )
    {
      // If a SpatialSurfaceObserver exists, we need to unregister from event notifications before releasing it.
      if ( m_surfaceObserver != nullptr )
      {
        m_surfaceObserver->ObservedSurfacesChanged -= m_surfaceObserverEventToken;
        m_surfaceObserver = nullptr;
      }

      // The spatial mapping API reads information about the user's environment. The user must
      // grant permission to the app to use this capability of the Windows Holographic device.
      auto initSurfaceObserverTask = create_task( SpatialSurfaceObserver::RequestAccessAsync() );
      initSurfaceObserverTask.then( [this, coordinateSystem]( Windows::Perception::Spatial::SpatialPerceptionAccessStatus status )
      {
        switch ( status )
        {
        case SpatialPerceptionAccessStatus::Allowed:
        {
          // If status is Allowed, we can create the surface observer.
          {
            // First, we'll set up the surface observer to use our preferred data formats.
            // In this example, a "preferred" format is chosen that is compatible with our pre-compiled shader pipeline.
            m_surfaceMeshOptions = ref new SpatialSurfaceMeshOptions();
            IVectorView<DirectXPixelFormat>^ supportedVertexPositionFormats = m_surfaceMeshOptions->SupportedVertexPositionFormats;
            unsigned int formatIndex = 0;
            if ( supportedVertexPositionFormats->IndexOf( DirectXPixelFormat::R32G32B32A32Float, &formatIndex ) )
            {
              m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R32G32B32A32Float;
            }
            IVectorView<DirectXPixelFormat>^ supportedVertexNormalFormats = m_surfaceMeshOptions->SupportedVertexNormalFormats;
            if ( supportedVertexNormalFormats->IndexOf( DirectXPixelFormat::R32G32B32A32Float, &formatIndex ) )
            {
              m_surfaceMeshOptions->VertexNormalFormat = DirectXPixelFormat::R32G32B32A32Float;
            }

            // Our shader pipeline can handle a variety of triangle index formats
            IVectorView<DirectXPixelFormat>^ supportedTriangleIndexFormats = m_surfaceMeshOptions->SupportedTriangleIndexFormats;
            if (supportedTriangleIndexFormats->IndexOf(DirectXPixelFormat::R32UInt, &formatIndex))
            {
                m_surfaceMeshOptions->TriangleIndexFormat = DirectXPixelFormat::R32UInt;
            }

            // Create the observer.
            m_surfaceObserver = ref new SpatialSurfaceObserver();
          }

          // The surface observer can now be configured as needed.
          UpdateSurfaceObserverPosition( coordinateSystem );
        }
        break;
        case SpatialPerceptionAccessStatus::DeniedBySystem:
        {
          OutputDebugString( L"Error: Cannot initialize surface observer because the system denied access to the spatialPerception capability.\n" );
        }
        break;
        case SpatialPerceptionAccessStatus::DeniedByUser:
        {
          OutputDebugString( L"Error: Cannot initialize surface observer because the user denied access to the spatialPerception capability.\n" );
        }
        break;
        case SpatialPerceptionAccessStatus::Unspecified:
        {
          OutputDebugString( L"Error: Cannot initialize surface observer. Access was denied for an unspecified reason.\n" );
        }
        break;
        default:
          // unreachable
          break;
        }

        if ( m_surfaceObserver != nullptr )
        {
          // If the surface observer was successfully created, we can initialize our
          // collection by pulling the current data set.
          auto mapContainingSurfaceCollection = m_surfaceObserver->GetObservedSurfaces();
          if (mapContainingSurfaceCollection->Size == 0)
          {
            OutputDebugStringA("Mesh collection size is 0. Trying again after a delay.\n");
            auto fire_once = new Concurrency::timer<int>(INIT_SURFACE_RETRY_DELAY_MS, 0, nullptr, false);
            // Create a call object that sets the completion event after the timer fires.
            auto callback = new Concurrency::call<int>([=](int)
            {
              this->InitializeSurfaceObserver(coordinateSystem);
            });

            // Connect the timer to the callback and start the timer.
            fire_once->link_target(callback);
            fire_once->start();
            return;
          }
          for ( auto const& pair : mapContainingSurfaceCollection )
          {
            // Store the ID and metadata for each surface.
            auto const& id = pair->Key;
            auto const& surfaceInfo = pair->Value;
            m_surfaceCollection->AddSurface( id, surfaceInfo, m_surfaceMeshOptions);
          }

          // We can also subscribe to an event to receive up-to-date data.
          m_surfaceObserverEventToken =
            m_surfaceObserver->ObservedSurfacesChanged +=
              ref new Windows::Foundation::TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(
                std::bind( &SpatialSurfaceAPI::OnSurfacesChanged, this, std::placeholders::_1, std::placeholders::_2 )
              );
        }
      } );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::SaveAppState()
    {
      task<SpatialAnchorStore^> requestTask( SpatialAnchorManager::RequestStoreAsync() );
      auto saveTask = requestTask.then( [&]( SpatialAnchorStore ^ store )
      {
        if ( store == nullptr )
        {
          return;
        }

        int i( 0 );
        for ( auto pair : m_spatialAnchors )
        {
          if ( !store->TrySave( pair->Key + i.ToString(), pair->Value ) )
          {
            std::wstringstream wss;
            wss << L"Unable to save spatial anchor " << pair->Key->Data() << i.ToString()->Data();
            OutputDebugStringW( wss.str().c_str() );
          }
        }

        return;
      } );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::LoadAppState()
    {
      m_spatialAnchors->Clear();

      task<SpatialAnchorStore^> requestTask( SpatialAnchorManager::RequestStoreAsync() );
      auto loadTask = requestTask.then( [&]( SpatialAnchorStore ^ store )
      {
        if ( store == nullptr )
        {
          return;
        }

        int i( 0 );
        Windows::Foundation::Collections::IMapView<Platform::String^, SpatialAnchor^>^ output = store->GetAllSavedAnchors();
        for ( auto pair : output )
        {
          m_spatialAnchors->Insert( pair->Key, pair->Value );
        }
      } );
    }
  }
}