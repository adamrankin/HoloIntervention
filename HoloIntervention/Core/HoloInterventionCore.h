/*====================================================================
Copyright(c) 2017 Adam Rankin


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

// Common includes
#include "DeviceResources.h"
#include "StepTimer.h"

// STL includes
#include <vector>

// WinRT includes
#include <collection.h>

namespace HoloIntervention
{
  class IConfigurable;
  class IEngineComponent;

  namespace Physics
  {
    class PhysicsAPI;
  }

  namespace System
  {
    class GazeSystem;
    class IconSystem;
    class ImagingSystem;
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;
    class SplashSystem;
    class ToolSystem;
  }

  namespace Input
  {
    class SpatialInput;
    class VoiceInput;
  }

  namespace Rendering
  {
    class ModelRenderer;
    class NotificationRenderer;
    class SliceRenderer;
    class MeshRenderer;
    class VolumeRenderer;
  }

  namespace Network
  {
    class IGTConnector;
  }

  namespace Sound
  {
    class SoundAPI;
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
    void SetHolographicFocusPoint(Windows::Graphics::Holographic::HolographicFramePrediction^ prediction,
                                  Windows::Graphics::Holographic::HolographicFrame^ holographicFrame,
                                  Windows::Perception::Spatial::SpatialCoordinateSystem^ currentCoordinateSystem,
                                  Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

    // Write the configuration to file
    concurrency::task<bool> WriteConfigurationAsync();

    concurrency::task<bool> ReadConfigurationAsync();

  protected:
    // IEngineComponent list, used to query overall system status
    std::vector<IEngineComponent*>                        m_engineComponents;
    std::vector<IConfigurable*>                           m_configurableComponents;

    // Renderers
    std::unique_ptr<Rendering::ModelRenderer>             m_modelRenderer;
    std::unique_ptr<Rendering::NotificationRenderer>      m_notificationRenderer;
    std::unique_ptr<Rendering::SliceRenderer>             m_sliceRenderer;
    std::unique_ptr<Rendering::MeshRenderer>              m_meshRenderer;
    std::unique_ptr<Rendering::VolumeRenderer>            m_volumeRenderer;

    // Event handlers
    std::unique_ptr<Input::SpatialInput>                  m_spatialInput;
    std::unique_ptr<Input::VoiceInput>                    m_voiceInput;

    // Engine state
    std::atomic_bool                                      m_engineReady = false;
    std::atomic_bool                                      m_engineUserEnabled = true;

    // Cached pointer to device resources.
    std::shared_ptr<DX::DeviceResources>                  m_deviceResources;

    // Render loop timer.
    DX::StepTimer                                         m_timer;

    // Represents the holographic space around the user.
    Windows::Graphics::Holographic::HolographicSpace^     m_holographicSpace;

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
    std::shared_ptr<System::NetworkSystem>                m_networkSystem;
    std::unique_ptr<System::GazeSystem>                   m_gazeSystem;
    std::unique_ptr<System::IconSystem>                   m_iconSystem;
    std::unique_ptr<System::ImagingSystem>                m_imagingSystem;
    std::unique_ptr<System::NotificationSystem>           m_notificationSystem;
    std::unique_ptr<System::RegistrationSystem>           m_registrationSystem;
    std::unique_ptr<System::SplashSystem>                 m_splashSystem;
    std::unique_ptr<System::ToolSystem>                   m_toolSystem;

    // Physics
    std::unique_ptr<Physics::PhysicsAPI>                  m_physicsAPI;

    // Sound
    std::unique_ptr<Sound::SoundAPI>                      m_soundAPI;
  };
}