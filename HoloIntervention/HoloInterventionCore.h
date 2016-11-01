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
#include "StepTimer.h"

// std includes
#include <vector>

// winrt includes
#include <collection.h>

// Forward declarations
namespace DX
{
  class DeviceResources;
}

namespace HoloIntervention
{
  namespace System
  {
    class GazeSystem;
    class NotificationSystem;
    class RegistrationSystem;
    class SpatialSystem;
    class ToolSystem;
    class ImagingSystem;
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
    class VolumeRenderer;
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
  class HoloInterventionCore : public DX::IDeviceNotify
  {
  public:
    HoloInterventionCore(const std::shared_ptr<DX::DeviceResources>& deviceResources);
    ~HoloInterventionCore();

    // Sets the holographic space. This is our closest analogue to setting a new window
    // for the app.
    void SetHolographicSpace(Windows::Graphics::Holographic::HolographicSpace^ holographicSpace);

    // Starts the holographic frame and updates the content.
    Windows::Graphics::Holographic::HolographicFrame^ Update();

    // Renders holograms, including world-locked content.
    bool Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame);

    // Handle saving and loading of app state owned by AppMain.
    Concurrency::task<void> SaveAppStateAsync();
    Concurrency::task<void> LoadAppStateAsync();

    // Global access to the current frame number
    uint64 GetCurrentFrameNumber() const;

    // Provide access to the logic systems
    System::GazeSystem& GetGazeSystem();
    System::ImagingSystem& GetImagingSystem();
    System::NotificationSystem& GetNotificationsSystem();
    System::RegistrationSystem& GetRegistrationSystem();
    System::SpatialSystem& GetSpatialSystem();
    System::ToolSystem& GetToolSystem();

    // Provide access to the sound manager
    Sound::SoundManager& GetSoundManager();

    // Provide access to the network link
    Network::IGTLinkIF& GetIGTLink();

    // Provide access to the renderers
    Rendering::ModelRenderer& GetModelRenderer();
    Rendering::SliceRenderer& GetSliceRenderer();
    Rendering::VolumeRenderer& GetVolumeRenderer();

    // IDeviceNotify
    virtual void OnDeviceLost();
    virtual void OnDeviceRestored();

  protected:
    // Asynchronously creates resources for new holographic cameras.
    void OnCameraAdded(Windows::Graphics::Holographic::HolographicSpace^ sender, Windows::Graphics::Holographic::HolographicSpaceCameraAddedEventArgs^ args);

    // Synchronously releases resources for holographic cameras that are no longer attached to the system.
    void OnCameraRemoved(Windows::Graphics::Holographic::HolographicSpace^ sender, Windows::Graphics::Holographic::HolographicSpaceCameraRemovedEventArgs^ args);

    // Used to notify the app when the positional tracking state changes.
    void OnLocatabilityChanged(Windows::Perception::Spatial::SpatialLocator^ sender, Platform::Object^ args);

    // Clears event registration state. Used when changing to a new HolographicSpace and when tearing down AppMain.
    void UnregisterHolographicEventHandlers();

    // Check for any voice input commands
    void InitializeVoiceSystem();

    // Set the focus point depending on the state of all the systems
    void SetHolographicFocusPoint(Windows::Graphics::Holographic::HolographicFramePrediction^ prediction, Windows::Graphics::Holographic::HolographicFrame^ holographicFrame, Windows::Perception::Spatial::SpatialCoordinateSystem^ currentCoordinateSystem);

  protected:
    // Renderers
    std::unique_ptr<Rendering::ModelRenderer>             m_modelRenderer;
    std::unique_ptr<Rendering::SliceRenderer>             m_sliceRenderer;
    std::unique_ptr<Rendering::SpatialMeshRenderer>       m_meshRenderer;
    std::unique_ptr<Rendering::VolumeRenderer>            m_volumeRenderer;

    // Event handlers
    std::unique_ptr<Input::SpatialInputHandler>           m_spatialInputHandler;
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
    HolographicSpace^                                     m_holographicSpace;

    // SpatialLocator that is attached to the primary camera.
    Windows::Perception::Spatial::SpatialLocator^         m_locator;

    // A reference frame attached to the holographic camera.
    Windows::Perception::Spatial::SpatialLocatorAttachedFrameOfReference^ m_attachedReferenceFrame;

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
    std::unique_ptr<System::RegistrationSystem>           m_registrationSystem;
    std::unique_ptr<System::ImagingSystem>                m_imagingSystem;

    // Sound assets
    std::unique_ptr<Sound::SoundManager>                  m_soundManager;
  };
}