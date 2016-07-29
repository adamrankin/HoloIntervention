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
#include "DirectXHelper.h"
#include "TrackedUltrasoundMain.h"

// std includes
#include <string>

// winrt includes
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace concurrency;

namespace TrackedUltrasound
{
  //----------------------------------------------------------------------------
  // Loads and initializes application assets when the application is loaded.
  TrackedUltrasoundMain::TrackedUltrasoundMain( const std::shared_ptr<DX::DeviceResources>& deviceResources )
    : m_deviceResources( deviceResources )
    , m_cursorSound( nullptr )
  {
    // Register to be notified if the device is lost or recreated.
    m_deviceResources->RegisterDeviceNotify( this );

    m_cursorSound = std::make_unique<TrackedUltrasound::Sound::OmnidirectionalSound>();
    // TODO : magic values for now
    auto loadTask = m_cursorSound->InitializeAsync( L"Assets/Sounds/cursor_toggle.wav", 2, 3, 1 );
    loadTask.then( [&]( task<HRESULT> previousTask )
    {
      try
      {
        previousTask.wait();
        m_cursorSound->SetEnvironment( HrtfEnvironment::Small );
      }
      catch ( Platform::Exception^ e )
      {
        OutputDebugStringW( e->Message->Data() );
      }
      catch ( const std::exception& e )
      {
        OutputDebugStringA( e.what() );
        m_cursorSound.reset();
      }
    } );
  }

  //----------------------------------------------------------------------------
  TrackedUltrasoundMain::~TrackedUltrasoundMain()
  {
    // De-register device notification.
    m_deviceResources->RegisterDeviceNotify( nullptr );

    UnregisterHolographicEventHandlers();
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::SetHolographicSpace( HolographicSpace^ holographicSpace )
  {
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    // Initialize the system components
    m_gazeCursorRenderer = std::make_unique<Rendering::GazeCursorRenderer>( m_deviceResources );
    m_spatialInputHandler = std::make_unique<Input::SpatialInputHandler>();
    m_voiceInputHandler = std::make_unique<Input::VoiceInputHandler>();
    m_spatialSurfaceApi = std::make_unique<Spatial::SpatialSurfaceAPI>( m_deviceResources );

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    m_locatabilityChangedToken =
      m_locator->LocatabilityChanged +=
        ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^>(
          std::bind( &TrackedUltrasoundMain::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_cameraAddedToken =
      m_holographicSpace->CameraAdded +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(
          std::bind( &TrackedUltrasoundMain::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_cameraRemovedToken =
      m_holographicSpace->CameraRemoved +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(
          std::bind( &TrackedUltrasoundMain::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2 )
        );

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();
    m_stationaryReferenceFrame = m_locator->CreateStationaryFrameOfReferenceAtCurrentLocation();

    m_spatialSurfaceApi->InitializeSurfaceObserver( m_stationaryReferenceFrame->CoordinateSystem );
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::UnregisterHolographicEventHandlers()
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
  HolographicFrame^ TrackedUltrasoundMain::Update()
  {
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    m_deviceResources->EnsureCameraResources( holographicFrame, prediction );

    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp( prediction->Timestamp );

    // Check for new input state since the last frame.
    if (!m_voiceInputHandler->GetLastCommand().empty())
    {
      if (m_voiceInputHandler->GetLastCommand().compare(L"show") == 0)
      {
        m_cursorSound->StartOnce();
        m_gazeCursorRenderer->EnableCursor(true);
      }
      else if (m_voiceInputHandler->GetLastCommand().compare(L"hide") == 0)
      {
        m_cursorSound->StartOnce();
        m_gazeCursorRenderer->EnableCursor(false);
      }

      // Mark the command as handled
      m_voiceInputHandler->MarkCommandProcessed();
    }

    // Time-based updates
    m_timer.Tick( [&]()
    {
      m_cursorSound->Update( m_timer );
      m_spatialSurfaceApi->Update( m_timer, currentCoordinateSystem );

      // Update the gaze vector in the gaze renderer
      if ( m_gazeCursorRenderer->IsCursorEnabled() )
      {
        SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp( currentCoordinateSystem, prediction->Timestamp );
        const float3 position = pose->Head->Position;
        const float3 direction = pose->Head->ForwardDirection;

        float3 outHitPosition;
        float3 outHitNormal;
        bool hit = m_spatialSurfaceApi->TestRayIntersection( position, direction, outHitPosition, outHitNormal );

        if ( hit )
        {
          m_gazeCursorRenderer->Update( outHitPosition, outHitNormal );
        }
      }
    } );

    // We complete the frame update by using information about our content positioning to set the focus point.
    for ( auto cameraPose : prediction->CameraPoses )
    {
      HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters( cameraPose );

      if ( m_gazeCursorRenderer->IsCursorEnabled() )
      {
        // Set the focus to be the cursor
        try
        {
          renderingParameters->SetFocusPoint( currentCoordinateSystem, m_gazeCursorRenderer->GetPosition(), m_gazeCursorRenderer->GetNormal() );
        }
        catch (Platform::InvalidArgumentException ^ iex)
        {
          continue;
        }
        catch ( Platform::Exception^ ex )
        {
          // Turn the cursor off and output the message
          m_gazeCursorRenderer->ToggleCursor();
          OutputDebugStringW( ex->Message->Data() );
        }
      }
      else
      {
        // TODO : update to be the position of the slice renderer
        // TODO : implement slice renderer
      }
    }

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  // Renders the current frame to each holographic camera, according to the
  // current application and spatial positioning state. Returns true if the
  // frame was rendered to at least one camera.
  bool TrackedUltrasoundMain::Render( Windows::Graphics::Holographic::HolographicFrame^ holographicFrame )
  {
    // Don't try to render anything before the first Update.
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

        // The view and projection matrices for each holographic camera will change
        // every frame. This function refreshes the data in the constant buffer for
        // the holographic camera indicated by cameraPose.
        pCameraResources->UpdateViewProjectionBuffer( m_deviceResources, cameraPose, currentCoordinateSystem );
        bool activeCamera = pCameraResources->AttachViewProjectionBuffer( m_deviceResources );

        // Only render world-locked content when positional tracking is active.

        // Draw the gaze cursor if it's active
        if ( m_gazeCursorRenderer->IsCursorEnabled() && activeCamera && m_locatability == Windows::Perception::Spatial::SpatialLocatability::PositionalTrackingActive )
        {
          m_gazeCursorRenderer->Render();
        }

        atLeastOneCameraRendered = true;
      }

      return atLeastOneCameraRendered;
    } );
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::SaveAppState()
  {
    m_spatialSurfaceApi->SaveAppState();
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::LoadAppState()
  {
    m_spatialSurfaceApi->LoadAppState();
  }

  //----------------------------------------------------------------------------
  // Notifies classes that use Direct3D device resources that the device resources
  // need to be released before this method returns.
  void TrackedUltrasoundMain::OnDeviceLost()
  {
    m_gazeCursorRenderer->ReleaseDeviceDependentResources();
    // TODO : add ondevice calls to APIs that use directx resources
  }

  //----------------------------------------------------------------------------
  // Notifies classes that use Direct3D device resources that the device resources
  // may now be recreated.
  void TrackedUltrasoundMain::OnDeviceRestored()
  {
    m_gazeCursorRenderer->CreateDeviceDependentResourcesAsync();
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::OnLocatabilityChanged( SpatialLocator^ sender, Object^ args )
  {
    m_locatability = sender->Locatability;

    switch ( sender->Locatability )
    {
    case SpatialLocatability::Unavailable:
      // Holograms cannot be rendered.
    {
      String^ message = L"Warning! Positional tracking is " +
                        sender->Locatability.ToString() + L".\n";
      OutputDebugStringW( message->Data() );
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
  void TrackedUltrasoundMain::OnCameraAdded(
    HolographicSpace^ sender,
    HolographicSpaceCameraAddedEventArgs^ args
  )
  {
    Deferral^ deferral = args->GetDeferral();
    HolographicCamera^ holographicCamera = args->Camera;
    create_task( [this, deferral, holographicCamera]()
    {
      //
      // TODO: Allocate resources for the new camera and load any content specific to
      //       that camera. Note that the render target size (in pixels) is a property
      //       of the HolographicCamera object, and can be used to create off-screen
      //       render targets that match the resolution of the HolographicCamera.
      //

      // Create device-based resources for the holographic camera and add it to the list of
      // cameras used for updates and rendering. Notes:
      //   * Since this function may be called at any time, the AddHolographicCamera function
      //     waits until it can get a lock on the set of holographic camera resources before
      //     adding the new camera. At 60 frames per second this wait should not take long.
      //   * A subsequent Update will take the back buffer from the RenderingParameters of this
      //     camera's CameraPose and use it to create the ID3D11RenderTargetView for this camera.
      //     Content can then be rendered for the HolographicCamera.
      m_deviceResources->AddHolographicCamera( holographicCamera );

      // Holographic frame predictions will not include any information about this camera until
      // the deferral is completed.
      deferral->Complete();
    } );
  }

  //----------------------------------------------------------------------------
  void TrackedUltrasoundMain::OnCameraRemoved(
    HolographicSpace^ sender,
    HolographicSpaceCameraRemovedEventArgs^ args
  )
  {
    create_task( [this]()
    {
      //
      // TODO: Asynchronously unload or deactivate content resources (not back buffer
      //       resources) that are specific only to the camera that was removed.
      //
    } );

    // Before letting this callback return, ensure that all references to the back buffer are released.
    // Since this function may be called at any time, the RemoveHolographicCamera function
    // waits until it can get a lock on the set of holographic camera resources before
    // deallocating resources for this camera. At 60 frames per second this wait should not take long.
    m_deviceResources->RemoveHolographicCamera( args->Camera );
  }
}