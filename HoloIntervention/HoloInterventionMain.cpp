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

// local includes
#include "pch.h"
#include "Common.h"
#include "DirectXHelper.h"
#include "HoloInterventionMain.h"

// System includes
#include "NotificationSystem.h"
#include "SpatialSystem.h"

// Sound includes
#include "OmnidirectionalSound.h"

// Rendering includes
#include "SliceRenderer.h"
#include "ModelRenderer.h"
#include "NotificationRenderer.h"

// Network includes
#include "IGTLinkIF.h"

// Input includes
#include "SpatialInputHandler.h"
#include "VoiceInputHandler.h"

// std includes
#include <string>

// Windows includes
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace Concurrency;
using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::System::Threading;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  // Loads and initializes application assets when the application is loaded.
  HoloInterventionMain::HoloInterventionMain( const std::shared_ptr<DX::DeviceResources>& deviceResources )
    : m_deviceResources( deviceResources )
    , m_sliceToken( 0 )
    , m_cursorSound( nullptr )
    , m_latestFrame( ref new UWPOpenIGTLink::TrackedFrame() )
  {
    // Register to be notified if the device is lost or recreated.
    m_deviceResources->RegisterDeviceNotify( this );
  }

  //----------------------------------------------------------------------------
  HoloInterventionMain::~HoloInterventionMain()
  {
    // De-register device notification.
    m_deviceResources->RegisterDeviceNotify( nullptr );

    UnregisterHolographicEventHandlers();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::SetHolographicSpace( HolographicSpace^ holographicSpace )
  {
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    // Initialize the system components
    m_modelRenderer = std::make_unique<Rendering::ModelRenderer>( m_deviceResources );
    m_sliceRenderer = std::make_unique<Rendering::SliceRenderer>( m_deviceResources );
    m_notificationSystem = std::make_unique<Notifications::NotificationSystem>( m_deviceResources );
    m_spatialInputHandler = std::make_unique<Input::SpatialInputHandler>();
    m_voiceInputHandler = std::make_unique<Input::VoiceInputHandler>();
    m_spatialSystem = std::make_unique<Spatial::SpatialSystem>( m_deviceResources );
    m_igtLinkIF = std::make_unique<Network::IGTLinkIF>();
    // TODO : remove temp code
    m_igtLinkIF->SetHostname( L"172.16.80.1" );

    TimeSpan ts;
    ts.Duration = 10000000; // in 100-nanosecond units (1s)
    TimerElapsedHandler^ handler = ref new TimerElapsedHandler( [this]( ThreadPoolTimer ^ timer ) ->void
    {
      m_igtLinkIF->ConnectAsync().then( [this]( bool result )
      {
        if ( result )
        {
          m_sliceToken = m_sliceRenderer->AddSlice();
          m_notificationSystem->QueueMessage( L"Connected." );
          m_sliceRenderer->SetSliceVisible( m_sliceToken, true );
        }
      } );
    } );
    ThreadPoolTimer^ timer = ThreadPoolTimer::CreateTimer( handler, ts );

    m_modelToken = m_modelRenderer->AddModel( L"Assets/Models/gaze_cursor.cmo" );

    InitializeAudioAssetsAsync();
    InitializeVoiceSystem();

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    m_locatabilityChangedToken =
      m_locator->LocatabilityChanged +=
        ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^>(
          std::bind( &HoloInterventionMain::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_cameraAddedToken =
      m_holographicSpace->CameraAdded +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(
          std::bind( &HoloInterventionMain::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_cameraRemovedToken =
      m_holographicSpace->CameraRemoved +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(
          std::bind( &HoloInterventionMain::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();
    m_stationaryReferenceFrame = m_locator->CreateStationaryFrameOfReferenceAtCurrentLocation();

    m_spatialSystem->InitializeSurfaceObserver( m_stationaryReferenceFrame->CoordinateSystem );

    // Create a bogus frame to grab sensor data
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp( prediction->Timestamp );

    SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp( currentCoordinateSystem, prediction->Timestamp );
    m_notificationSystem->Initialize( pose );
  }

  //----------------------------------------------------------------------------
  Concurrency::task<void> HoloInterventionMain::InitializeAudioAssetsAsync()
  {
    return create_task( [this]()
    {
      m_cursorSound = std::make_unique<HoloIntervention::Sound::OmnidirectionalSound>();

      auto loadTask = m_cursorSound->InitializeAsync( L"Assets/Sounds/input_ok.mp3" );
      loadTask.then( [&]( task<HRESULT> previousTask )
      {
        try
        {
          previousTask.wait();
        }
        catch ( Platform::Exception^ e )
        {
          m_notificationSystem->QueueMessage( e->Message );
        }
        catch ( const std::exception& e )
        {
          m_notificationSystem->QueueMessage( e.what() );
          m_cursorSound.reset();
        }

        m_cursorSound->SetEnvironment( HrtfEnvironment::Small );
      } );
    } );
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::UnregisterHolographicEventHandlers()
  {
    if ( m_holographicSpace != nullptr )
    {
      // Clear previous event registrations.

      if ( m_cameraAddedToken.Value != 0 )
      {
        m_holographicSpace->CameraAdded -= m_cameraAddedToken;
        m_cameraAddedToken.Value = 0;
      }

      if ( m_cameraRemovedToken.Value != 0 )
      {
        m_holographicSpace->CameraRemoved -= m_cameraRemovedToken;
        m_cameraRemovedToken.Value = 0;
      }
    }

    if ( m_locator != nullptr )
    {
      m_locator->LocatabilityChanged -= m_locatabilityChangedToken;
    }
  }

  //----------------------------------------------------------------------------
  // Updates the application state once per frame.
  HolographicFrame^ HoloInterventionMain::Update()
  {
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    m_deviceResources->EnsureCameraResources( holographicFrame, prediction );

    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp( prediction->Timestamp );

    // Time-based updates
    m_timer.Tick( [&]()
    {
      SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp( currentCoordinateSystem, prediction->Timestamp );

      m_cursorSound->Update( m_timer );
      m_spatialSystem->Update( m_timer, currentCoordinateSystem );
      m_sliceRenderer->Update( pose, m_timer );
      m_notificationSystem->Update( pose, m_timer );

      if ( m_igtLinkIF->IsConnected() )
      {
        if ( m_igtLinkIF->GetLatestTrackedFrame( m_latestFrame ) )
        {
          m_sliceRenderer->UpdateSlice( m_sliceToken,
                                        *( std::shared_ptr<byte*>* )m_latestFrame->ImageDataSharedPtr,
                                        m_latestFrame->Width,
                                        m_latestFrame->Height,
                                        ( DXGI_FORMAT )m_latestFrame->PixelFormat,
                                        m_latestFrame->EmbeddedImageTransform );
        }
      }

      // Update the gaze vector in the gaze renderer
      if ( m_modelRenderer->GetModel( m_modelToken )->IsModelEnabled() )
      {
        const float3 position = pose->Head->Position;
        const float3 direction = pose->Head->ForwardDirection;

        float3 outHitPosition;
        float3 outHitNormal;
        bool hit = m_spatialSystem->TestRayIntersection( position, direction, outHitPosition, outHitNormal );

        if ( hit )
        {
          // Update the gaze cursor renderer with the pose to render
          // TODO : update the world matrix of the model
          m_modelRenderer->Update( m_timer );
        }
      }
    } );

    // We complete the frame update by using information about our content positioning to set the focus point.
    for ( auto cameraPose : prediction->CameraPoses )
    {
      HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters( cameraPose );

      if ( m_notificationSystem->IsShowingNotification() )
      {
        float3 const& focusPointPosition = m_notificationSystem->GetPosition();
        float3        focusPointNormal = ( focusPointPosition == float3( 0.f ) ) ? float3( 0.f, 0.f, 1.f ) : -normalize( focusPointPosition );
        float3 const& focusPointVelocity = m_notificationSystem->GetVelocity();

        renderingParameters->SetFocusPoint(
          currentCoordinateSystem,
          focusPointPosition,
          focusPointNormal,
          focusPointVelocity
        );
      }
      else if ( m_modelRenderer->GetModel( m_modelToken )->IsModelEnabled() )
      {
        // Set the focus to be the cursor
        try
        {
          //renderingParameters->SetFocusPoint( currentCoordinateSystem, m_gazeCursorRenderer->GetPosition(), m_gazeCursorRenderer->GetNormal() );
        }
        catch ( Platform::InvalidArgumentException^ iex )
        {
          continue;
        }
        catch ( Platform::Exception^ ex )
        {
          // Turn the cursor off and output the message
          m_modelRenderer->GetModel( m_modelToken )->ToggleEnabled();
          m_notificationSystem->QueueMessage( ex->Message );
        }
      }
      else if( m_sliceToken != 0 )
      {
        float4x4 mat;
        if ( m_sliceRenderer->GetSlicePose( m_sliceToken, mat ) )
        {
          SimpleMath::Matrix matrix;
          XMStoreFloat4x4( &matrix, XMMatrixTranspose( XMLoadFloat4x4( &mat ) ) );
          SimpleMath::Vector3 translation;
          SimpleMath::Vector3 scale;
          SimpleMath::Quaternion rotation;
          matrix.Decompose( scale, rotation, translation );

          float3 focusPointPosition( translation.x, translation.y, translation.z );
          float3 focusPointNormal = ( focusPointPosition == float3( 0.f ) ) ? float3( 0.f, 0.f, 1.f ) : -normalize( focusPointPosition );
          // TODO : store velocity of slice for stabilization?
          float3 focusPointVelocity = float3( 0.f );

          renderingParameters->SetFocusPoint(
            currentCoordinateSystem,
            focusPointPosition,
            focusPointNormal,
            focusPointVelocity );
        }
      }
    }

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  // Renders the current frame to each holographic camera, according to the
  // current application and spatial positioning state. Returns true if the
  // frame was rendered to at least one camera.
  bool HoloInterventionMain::Render( Windows::Graphics::Holographic::HolographicFrame^ holographicFrame )
  {
    if ( m_timer.GetFrameCount() == 0 )
    {
      return false;
    }

    // Lock the set of holographic camera resources, then draw to each camera in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool>(
             [this, holographicFrame]( std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap )
    {
      // Up-to-date frame predictions enhance the effectiveness of image stabilization and
      // allow more accurate positioning of holograms.
      holographicFrame->UpdateCurrentPrediction();
      HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

      SpatialCoordinateSystem^ currentCoordinateSystem =
        m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp( prediction->Timestamp );

      bool atLeastOneCameraRendered = false;
      for ( auto cameraPose : prediction->CameraPoses )
      {
        // This represents the device-based resources for a HolographicCamera.
        DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

        // Get the device context.
        const auto context = m_deviceResources->GetD3DDeviceContext();
        const auto depthStencilView = pCameraResources->GetDepthStencilView();

        // Set render targets to the current holographic camera.
        ID3D11RenderTargetView* const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
        context->OMSetRenderTargets( 1, targets, depthStencilView );

        // Clear the back buffer and depth stencil view.
        context->ClearRenderTargetView( targets[0], DirectX::Colors::Transparent );
        context->ClearDepthStencilView( depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0 );

        pCameraResources->UpdateViewProjectionBuffer( m_deviceResources, cameraPose, currentCoordinateSystem );
        bool activeCamera = pCameraResources->AttachViewProjectionBuffer( m_deviceResources );

        // Only render world-locked content when positional tracking is active.

        // Draw all models
        m_modelRenderer->Render();

        if ( m_notificationSystem->IsShowingNotification() )
        {
          m_notificationSystem->GetRenderer()->Render();
        }

        if ( m_sliceToken != 0 )
        {
          m_sliceRenderer->Render();
        }
        atLeastOneCameraRendered = true;
      }

      return atLeastOneCameraRendered;
    } );
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::SaveAppState()
  {
    m_spatialSystem->SaveAppState();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::LoadAppState()
  {
    m_spatialSystem->LoadAppState();
  }

  //----------------------------------------------------------------------------
  Notifications::NotificationSystem& HoloInterventionMain::GetNotificationsAPI()
  {
    return *m_notificationSystem.get();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::OnDeviceLost()
  {
    m_spatialSystem->ReleaseDeviceDependentResources();
    m_modelRenderer->ReleaseDeviceDependentResources();
    m_sliceRenderer->ReleaseDeviceDependentResources();
    m_notificationSystem->ReleaseDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::OnDeviceRestored()
  {
    m_modelRenderer->CreateDeviceDependentResources();
    m_sliceRenderer->CreateDeviceDependentResources();
    m_notificationSystem->CreateDeviceDependentResources();
    m_spatialSystem->CreateDeviceDependentResourcesAsync();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::OnLocatabilityChanged( SpatialLocator^ sender, Object^ args )
  {
    m_locatability = sender->Locatability;

    switch ( sender->Locatability )
    {
    case SpatialLocatability::Unavailable:
      // Holograms cannot be rendered.
    {
      String^ message = L"Warning! Positional tracking is " +
                        sender->Locatability.ToString() + L".\n";
      m_notificationSystem->QueueMessage( message );
    }
    break;

    // In the following three cases, it is still possible to place holograms using a
    // SpatialLocatorAttachedFrameOfReference.
    case SpatialLocatability::PositionalTrackingActivating:
    // The system is preparing to use positional tracking.

    case SpatialLocatability::OrientationOnly:
    // Positional tracking has not been activated.

    case SpatialLocatability::PositionalTrackingInhibited:
      // Positional tracking is temporarily inhibited. User action may be required
      // in order to restore positional tracking.
      break;

    case SpatialLocatability::PositionalTrackingActive:
      // Positional tracking is active. World-locked content can be rendered.
      break;
    }
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::OnCameraAdded(
    HolographicSpace^ sender,
    HolographicSpaceCameraAddedEventArgs^ args
  )
  {
    Deferral^ deferral = args->GetDeferral();
    HolographicCamera^ holographicCamera = args->Camera;
    create_task( [this, deferral, holographicCamera]()
    {
      m_deviceResources->AddHolographicCamera( holographicCamera );

      // Holographic frame predictions will not include any information about this camera until the deferral is completed.
      deferral->Complete();
    } );
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::OnCameraRemoved(
    HolographicSpace^ sender,
    HolographicSpaceCameraRemovedEventArgs^ args
  )
  {
    create_task( [this]()
    {
      // TODO: Asynchronously unload or deactivate content resources (not back buffer
      //       resources) that are specific only to the camera that was removed.
    } );

    m_deviceResources->RemoveHolographicCamera( args->Camera );
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::InitializeVoiceSystem()
  {
    Input::VoiceInputCallbackMap callbacks;

    callbacks[L"show"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_modelRenderer->GetModel( m_modelToken )->EnableModel( true );
      m_notificationSystem->QueueMessage( L"Cursor on." );
    };

    callbacks[L"hide"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_modelRenderer->GetModel( m_modelToken )->EnableModel( false );
      m_notificationSystem->QueueMessage( L"Cursor off." );
    };

    callbacks[L"connect"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_notificationSystem->QueueMessage( L"Connecting..." );
      m_igtLinkIF->ConnectAsync( 4.0 ).then( [this]( bool result )
      {
        if ( result )
        {
          m_notificationSystem->QueueMessage( L"Connection successful." );
        }
        else
        {
          m_notificationSystem->QueueMessage( L"Connection failed." );
        }
      } );
    };

    callbacks[L"disconnect"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_notificationSystem->QueueMessage( L"Disconnected." );
      m_igtLinkIF->Disconnect();
    };

    callbacks[L"zoom in"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_notificationSystem->QueueMessage( L"Zooming in." );
      m_sliceRenderer->SetSliceHeadlocked( m_sliceToken, true );
    };

    callbacks[L"zoom out"] = [this]()
    {
      m_cursorSound->StartOnce();
      m_notificationSystem->QueueMessage( L"Zooming out." );
      m_sliceRenderer->SetSliceHeadlocked( m_sliceToken, false );
    };
    m_voiceInputHandler->RegisterCallbacks( callbacks );
  }

  //----------------------------------------------------------------------------
  void HoloInterventionMain::TrackedFrameCallback( UWPOpenIGTLink::TrackedFrame^ frame )
  {
    if ( m_sliceToken == 0 )
    {
      // For now, our slice renderer only draws one slice, in the future, it will have to draw more
      m_sliceToken = m_sliceRenderer->AddSlice( *( std::shared_ptr<uint8*>* )frame->ImageDataSharedPtr, frame->FrameSize->GetAt( 0 ), frame->FrameSize->GetAt( 1 ), ( DXGI_FORMAT )frame->PixelFormat, frame->EmbeddedImageTransform );
      return;
    }

    m_sliceRenderer->UpdateSlice( m_sliceToken, *( std::shared_ptr<uint8*>* )frame->ImageDataSharedPtr, frame->FrameSize->GetAt( 0 ), frame->FrameSize->GetAt( 1 ), ( DXGI_FORMAT )frame->PixelFormat, frame->EmbeddedImageTransform );
  }
}