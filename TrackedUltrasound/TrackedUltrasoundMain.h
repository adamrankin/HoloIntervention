#pragma once

// Local Includes
#include "DeviceResources.h"
#include "GazeCursorRenderer.h"
#include "OmnidirectionalSound.h"
#include "SpatialInputHandler.h"
#include "StepTimer.h"
#include "SpatialSurfaceAPI.h"

// std includes
#include <vector>

// winrt includes
#include <collection.h>

using namespace Windows::Perception::Spatial;
using namespace TrackedUltrasound::Spatial;

namespace TrackedUltrasound
{
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

    // IDeviceNotify
    virtual void OnDeviceLost();
    virtual void OnDeviceRestored();

  private:
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

    // Renderer for showing the gaze cursor
    std::unique_ptr<GazeCursorRenderer> m_gazeCursorRenderer;

    // Listens for the Pressed spatial input event.
    std::shared_ptr<SpatialInputHandler> m_spatialInputHandler;

    // Cached pointer to device resources.
    std::shared_ptr<DX::DeviceResources> m_deviceResources;

    // Render loop timer.
    DX::StepTimer m_timer;

    // Represents the holographic space around the user.
    Windows::Graphics::Holographic::HolographicSpace^ m_holographicSpace;

    // SpatialLocator that is attached to the primary camera.
    Windows::Perception::Spatial::SpatialLocator^ m_locator;

    // A reference frame attached to the holographic camera.
    Windows::Perception::Spatial::SpatialLocatorAttachedFrameOfReference^ m_attachedReferenceFrame;

    // A reference frame placed in the environment.
    Windows::Perception::Spatial::SpatialStationaryFrameOfReference^      m_stationaryReferenceFrame;

    // Event registration tokens.
    Windows::Foundation::EventRegistrationToken m_cameraAddedToken;
    Windows::Foundation::EventRegistrationToken m_cameraRemovedToken;
    Windows::Foundation::EventRegistrationToken m_locatabilityChangedToken;

    // Store the current state of locatability
    Windows::Perception::Spatial::SpatialLocatability m_locatability;

    // Access to a spatial surface API
    std::unique_ptr<SpatialSurfaceAPI> m_spatialSurfaceApi;

    // Sound assets
    std::unique_ptr<TrackedUltrasound::Sound::OmnidirectionalSound> m_cursorSound;
  };
}