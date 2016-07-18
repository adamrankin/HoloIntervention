#include "pch.h"

// Local includes
#include "SpatialSurfaceAPI.h"
#include "StepTimer.h"

// WinRT includes
#include <functional>
#include <ppltasks.h>
#include <sstream>

using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace concurrency;

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
      // Keep the surface observer positioned at the device's location.
      UpdateSurfaceObserverPosition( coordinateSystem );

      m_surfaceCollection->Update( timer, coordinateSystem );
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

        // Choose whether to add, or update the surface.
        // In this example, new surfaces are treated differently by highlighting them in a different
        // color. This allows you to observe changes in the spatial map that are due to new meshes,
        // as opposed to mesh updates.
        // In your app, you might choose to process added surfaces differently than updated
        // surfaces. For example, you might prioritize processing of added surfaces, and
        // defer processing of updates to existing surfaces.
        if ( m_surfaceCollection->HasSurface( id ) )
        {
          if ( m_surfaceCollection->GetLastUpdateTime( id ).UniversalTime < surfaceInfo->UpdateTime.UniversalTime )
          {
            // Update existing surface.
            m_surfaceCollection->UpdateSurface( id, surfaceInfo );
          }
        }
        else
        {
          // New surface.
          m_surfaceCollection->AddSurface( id, surfaceInfo );
        }
      }

      // Sometimes, a mesh will fall outside the area that is currently visible to
      // the surface observer. In this code sample, we "sleep" any meshes that are
      // not included in the surface collection to avoid rendering them.
      // The system can including them in the collection again later, in which case
      // they will no longer be hidden.
      m_surfaceCollection->HideInactiveMeshes( surfaceCollection );
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceAPI::UpdateSurfaceObserverPosition( SpatialCoordinateSystem^ coordinateSystem )
    {
      // In this example, we specify one area to be observed using an axis-aligned
      // bounding box 20 meters wide, and 5 meters tall, that is centered at the
      // origin of coordinateSystem.
      SpatialBoundingBox aabb =
      {
        { 0.f,  0.f, 0.f },
        { 20.f, 20.f, 5.f },
      };

      if ( m_surfaceObserver != nullptr )
      {
        SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromBox( coordinateSystem, aabb );
        m_surfaceObserver->SetBoundingVolume( bounds );

        // Note that it is possible to set multiple bounding volumes. Pseudocode:
        //     m_surfaceObserver->SetBoundingVolumes(/* iterable collection of bounding volumes*/);
        //
        // It is also possible to use other bounding shapes - such as a view frustum. Pseudocode:
        //     SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromFrustum(coordinateSystem, viewFrustum);
        //     m_surfaceObserver->SetBoundingVolume(bounds);
      }
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
            if ( supportedVertexPositionFormats->IndexOf( DirectXPixelFormat::R16G16B16A16IntNormalized, &formatIndex ) )
            {
              m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R16G16B16A16IntNormalized;
            }
            IVectorView<DirectXPixelFormat>^ supportedVertexNormalFormats = m_surfaceMeshOptions->SupportedVertexNormalFormats;
            if ( supportedVertexNormalFormats->IndexOf( DirectXPixelFormat::R8G8B8A8IntNormalized, &formatIndex ) )
            {
              m_surfaceMeshOptions->VertexNormalFormat = DirectXPixelFormat::R8G8B8A8IntNormalized;
            }

            // Our shader pipeline can handle a variety of triangle index formats, so we don't specify one here.
            // The code for doing so would be as follows:
            //IVectorView<DirectXPixelFormat>^ supportedTriangleIndexFormats = m_surfaceMeshOptions->SupportedTriangleIndexFormats;
            //if (supportedTriangleIndexFormats->IndexOf(DirectXPixelFormat::R16UInt, &formatIndex))
            //{
            //    m_surfaceMeshOptions->TriangleIndexFormat = DirectXPixelFormat::R16UInt;
            //}

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
          for ( auto const& pair : mapContainingSurfaceCollection )
          {
            // Store the ID and metadata for each surface.
            auto const& id = pair->Key;
            auto const& surfaceInfo = pair->Value;
            m_surfaceCollection->AddSurface( id, surfaceInfo );
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