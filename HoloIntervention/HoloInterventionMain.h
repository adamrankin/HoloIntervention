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
namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
    class SpatialSystem;
    class ToolSystem;
    class GazeSystem;
    class AnchorSystem;
  }

  namespace Input
  {
    class SpatialInputHandler;
    class VoiceInputHandler;
  }

  namespace Rendering
  {
    class ModelRenderer;
    class NotificationRenderer;
    class SliceRenderer;
    class SpatialMeshRenderer;
  }

  namespace Network
  {
    class IGTLinkIF;
  }

  namespace Sound
  {
    class SoundManager;
  }

  // Updates, renders, and presents holographic content using Direct3D.
  class HoloInterventionMain : public DX::IDeviceNotify
  {
  public:
    HoloInterventionMain( const std::shared_ptr<DX::DeviceResources>& deviceResources );
    ~HoloInterventionMain();

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

    // Global access to the current frame number
    uint64 GetCurrentFrameNumber() const;

    // Provide access to the logic systems
    System::NotificationSystem& GetNotificationsSystem();
    System::SpatialSystem& GetSpatialSystem();
    System::GazeSystem& GetGazeSystem();

    // Provide access to the sound manager
    Sound::SoundManager& GetSoundManager();

    // Provide access to the renderers
    Rendering::ModelRenderer& GetModelRenderer();
    Rendering::SliceRenderer& GetSliceRenderer();

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

    // Check for any voice input commands
    void InitializeVoiceSystem();

    // Callback when a frame is received by the system over IGTlink
    void TrackedFrameCallback( UWPOpenIGTLink::TrackedFrame^ frame );

  protected:
    // Renderers
    std::unique_ptr<Rendering::ModelRenderer>             m_modelRenderer;
    std::unique_ptr<Rendering::SliceRenderer>             m_sliceRenderer;
    std::unique_ptr<Rendering::SpatialMeshRenderer>       m_meshRenderer;

    // Mesh rendering control
    bool                                                  m_meshRendererEnabled = false;

    // Tokens
    uint32                                                m_sliceToken;

    // Spatial input event handler
    std::unique_ptr<Input::SpatialInputHandler>           m_spatialInputHandler;
    // Voice input event handler
    std::unique_ptr<Input::VoiceInputHandler>             m_voiceInputHandler;

    // Interface that manages a network connection to an IGT link server
    std::unique_ptr<Network::IGTLinkIF>                   m_igtLinkIF;
    UWPOpenIGTLink::TrackedFrame^                         m_latestFrame;
    double                                                m_latestTimestamp;

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

    // System pointers
    std::unique_ptr<System::SpatialSystem>                m_spatialSystem;
    std::unique_ptr<System::GazeSystem>                   m_gazeSystem;
    std::unique_ptr<System::ToolSystem>                   m_toolSystem;
    std::unique_ptr<System::NotificationSystem>           m_notificationSystem;

    // Sound assets
    std::unique_ptr<Sound::SoundManager>                  m_soundManager;
  };
}