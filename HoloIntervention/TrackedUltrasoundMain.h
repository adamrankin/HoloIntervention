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

// Local includes
#include "DeviceResources.h"
#include "StepTimer.h"

// std includes
#include <vector>

// winrt includes
#include <collection.h>

using namespace Windows::Perception::Spatial;

// Forward declarations
namespace TrackedUltrasound
{
  namespace Notifications
  {
    class NotificationSystem;
  }

  namespace Input
  {
    class SpatialInputHandler;
    class VoiceInputHandler;
  }

  namespace Spatial
  {
    class SpatialSystem;
  }

  namespace Rendering
  {
    class ModelRenderer;
    class NotificationRenderer;
    class SliceRenderer;
  }

  namespace Network
  {
    class IGTLinkIF;
  }

  namespace Sound
  {
    class OmnidirectionalSound;
  }

  // Updates, renders, and presents holographic content using Direct3D.
  class TrackedUltrasoundMain : public DX::IDeviceNotify
  {
  public:
    TrackedUltrasoundMain( const std::shared_ptr<DX::DeviceResources>& deviceResources );
    ~TrackedUltrasoundMain();

    // Sets the holographic space. This is our closest analogue to setting a new window
    // for the app.
    void SetHolographicSpace( Windows::Graphics::Holographic::HolographicSpace^ holographicSpace );

    // Starts the holographic frame and updates the content.
    Windows::Graphics::Holographic::HolographicFrame^ Update();

    // Renders holograms, including world-locked content.
    bool Render( Windows::Graphics::Holographic::HolographicFrame^ holographicFrame );

    // Handle saving and loading of app state owned by AppMain.
    void SaveAppState();
    void LoadAppState();

    // Provide access to the notifications API
    Notifications::NotificationSystem& GetNotificationsAPI();

    // IDeviceNotify
    virtual void OnDeviceLost();
    virtual void OnDeviceRestored();

  protected:
    // Asynchronously creates resources for new holographic cameras.
    void OnCameraAdded( Windows::Graphics::Holographic::HolographicSpace^ sender,
                        Windows::Graphics::Holographic::HolographicSpaceCameraAddedEventArgs^ args );

    // Synchronously releases resources for holographic cameras that are no longer
    // attached to the system.
    void OnCameraRemoved( Windows::Graphics::Holographic::HolographicSpace^ sender,
                          Windows::Graphics::Holographic::HolographicSpaceCameraRemovedEventArgs^ args );

    // Used to notify the app when the positional tracking state changes.
    void OnLocatabilityChanged( Windows::Perception::Spatial::SpatialLocator^ sender,
                                Platform::Object^ args );

    // Clears event registration state. Used when changing to a new HolographicSpace
    // and when tearing down AppMain.
    void UnregisterHolographicEventHandlers();

    // Initialize audio assets
    Concurrency::task<void> InitializeAudioAssetsAsync();

    // Check for any voice input commands
    void InitializeVoiceSystem();

    // Callback when a frame is received by the system over IGTlink
    void TrackedFrameCallback( UWPOpenIGTLink::TrackedFrame^ frame );

  protected:
    // Renderers
    std::unique_ptr<Rendering::ModelRenderer>             m_modelRenderer;
    std::unique_ptr<Rendering::SliceRenderer>             m_sliceRenderer;

    // Tokens
    uint32                                                m_sliceToken;
    uint32                                                m_modelToken;

    // Spatial input event handler
    std::unique_ptr<Input::SpatialInputHandler>           m_spatialInputHandler;
    // Voice input event handler
    std::unique_ptr<Input::VoiceInputHandler>             m_voiceInputHandler;

    // Interface that manages a network connection to an IGT link server
    std::unique_ptr<Network::IGTLinkIF>                   m_igtLinkIF;
    UWPOpenIGTLink::TrackedFrame^                         m_latestFrame;

    // Notification API
    std::unique_ptr<Notifications::NotificationSystem>    m_notificationSystem;

    // Cached pointer to device resources.
    std::shared_ptr<DX::DeviceResources>                  m_deviceResources;

    // Render loop timer.
    DX::StepTimer                                         m_timer;

    // Represents the holographic space around the user.
    Windows::Graphics::Holographic::HolographicSpace^     m_holographicSpace;

    // SpatialLocator that is attached to the primary camera.
    Windows::Perception::Spatial::SpatialLocator^         m_locator;

    // A reference frame attached to the holographic camera.
    Windows::Perception::Spatial::SpatialLocatorAttachedFrameOfReference^   m_attachedReferenceFrame;

    // A reference frame placed in the environment.
    Windows::Perception::Spatial::SpatialStationaryFrameOfReference^        m_stationaryReferenceFrame;

    // Event registration tokens.
    Windows::Foundation::EventRegistrationToken           m_cameraAddedToken;
    Windows::Foundation::EventRegistrationToken           m_cameraRemovedToken;
    Windows::Foundation::EventRegistrationToken           m_locatabilityChangedToken;
    uint64                                                m_trackedFrameReceivedToken;

    // Store the current state of locatability
    Windows::Perception::Spatial::SpatialLocatability     m_locatability;

    // Access to a spatial surface API
    std::unique_ptr<Spatial::SpatialSystem>               m_spatialSystem;

    // Sound assets
    std::unique_ptr<Sound::OmnidirectionalSound>          m_cursorSound;
  };
}