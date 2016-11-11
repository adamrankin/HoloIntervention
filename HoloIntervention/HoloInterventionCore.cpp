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
#include "AppView.h"
#include "Common.h"
#include "DirectXHelper.h"
#include "HoloInterventionCore.h"

// System includes
#include "GazeSystem.h"
#include "ImagingSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "SpatialSystem.h"
#include "ToolSystem.h"

// Sound includes
#include "SoundManager.h"

// Rendering includes
#include "ModelRenderer.h"
#include "NotificationRenderer.h"
#include "SliceRenderer.h"
#include "SpatialMeshRenderer.h"
#include "VolumeRenderer.h"

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
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::System::Threading;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  // Loads and initializes application assets when the application is loaded.
  HoloInterventionCore::HoloInterventionCore(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
    , m_latestFrame(ref new UWPOpenIGTLink::TrackedFrame())
  {
    // Register to be notified if the device is lost or recreated.
    m_deviceResources->RegisterDeviceNotify(this);
  }

  //----------------------------------------------------------------------------
  HoloInterventionCore::~HoloInterventionCore()
  {
    // De-register device notification.
    m_deviceResources->RegisterDeviceNotify(nullptr);

    UnregisterHolographicEventHandlers();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicSpace(HolographicSpace^ holographicSpace)
  {
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    // Initialize the system components
    m_modelRenderer = std::make_unique<Rendering::ModelRenderer>(m_deviceResources);
    m_sliceRenderer = std::make_unique<Rendering::SliceRenderer>(m_deviceResources);
    m_volumeRenderer = std::make_unique<Rendering::VolumeRenderer>(m_deviceResources);
    m_meshRenderer = std::make_unique<Rendering::SpatialMeshRenderer>(m_deviceResources);
    m_soundManager = std::make_unique<Sound::SoundManager>();

    m_notificationSystem = std::make_unique<System::NotificationSystem>(m_deviceResources);
    m_spatialInputHandler = std::make_unique<Input::SpatialInputHandler>();
    m_voiceInputHandler = std::make_unique<Input::VoiceInputHandler>();
    m_spatialSystem = std::make_unique<System::SpatialSystem>(m_deviceResources, m_timer);
    m_igtLinkIF = std::make_unique<Network::IGTLinkIF>();

    // Model renderer must come before the following systems
    m_gazeSystem = std::make_unique<System::GazeSystem>();
    m_toolSystem = std::make_unique<System::ToolSystem>();
    m_registrationSystem = std::make_unique<System::RegistrationSystem>(m_deviceResources, m_timer);
    m_imagingSystem = std::make_unique<System::ImagingSystem>();

    // TODO : remove temp code
    m_igtLinkIF->SetHostname(L"172.16.80.1");

    try
    {
      m_soundManager->InitializeAsync();
    }
    catch (Platform::Exception^ e)
    {
      m_notificationSystem->QueueMessage(L"Unable to initialize audio system. See log.");
      OutputDebugStringW((L"Audio Error" + e->Message)->Data());
    }

    InitializeVoiceSystem();

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    m_locatabilityChangedToken =
      m_locator->LocatabilityChanged +=
        ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^>(
          std::bind(&HoloInterventionCore::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2)
        );

    m_cameraAddedToken =
      m_holographicSpace->CameraAdded +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(
          std::bind(&HoloInterventionCore::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2)
        );

    m_cameraRemovedToken =
      m_holographicSpace->CameraRemoved +=
        ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(
          std::bind(&HoloInterventionCore::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2)
        );

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();

    // Initialize the notification system with a bogus frame to grab sensor data
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(holographicFrame->CurrentPrediction->Timestamp);
    SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp(currentCoordinateSystem, holographicFrame->CurrentPrediction->Timestamp);
    m_notificationSystem->Initialize(pose);
    m_spatialSystem->InitializeSurfaceObserver(m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(holographicFrame->CurrentPrediction->Timestamp));
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::UnregisterHolographicEventHandlers()
  {
    if (m_holographicSpace != nullptr)
    {
      // Clear previous event registrations.

      if (m_cameraAddedToken.Value != 0)
      {
        m_holographicSpace->CameraAdded -= m_cameraAddedToken;
        m_cameraAddedToken.Value = 0;
      }

      if (m_cameraRemovedToken.Value != 0)
      {
        m_holographicSpace->CameraRemoved -= m_cameraRemovedToken;
        m_cameraRemovedToken.Value = 0;
      }
    }

    if (m_locator != nullptr)
    {
      m_locator->LocatabilityChanged -= m_locatabilityChangedToken;
    }
  }

  //----------------------------------------------------------------------------
  // Updates the application state once per frame.
  HolographicFrame^ HoloInterventionCore::Update()
  {
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

    DX::ViewProjection vp;
    DX::CameraResources* cameraResources(nullptr);
    m_deviceResources->UseHolographicCameraResources<bool>(
      [this, holographicFrame, prediction, currentCoordinateSystem, &vp, &cameraResources](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
    {
      for (auto cameraPose : prediction->CameraPoses)
      {
        cameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();
        auto result = cameraResources->UpdateViewProjectionBuffer(m_deviceResources, cameraPose, currentCoordinateSystem, vp);
      }
      return true;
    });

    // Time-based updates
    m_timer.Tick([&]()
    {
      SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp(currentCoordinateSystem, prediction->Timestamp);

      if (m_igtLinkIF->IsConnected())
      {
        if (m_igtLinkIF->GetTrackedFrame(m_latestFrame, &m_latestTimestamp))
        {
          m_latestTimestamp = m_latestFrame->Timestamp;
          m_volumeRenderer->Update(m_latestFrame, m_timer, cameraResources);
          m_imagingSystem->Update(m_latestFrame, m_timer);
          m_toolSystem->Update(m_latestFrame, m_timer);
        }
      }

      m_spatialSystem->Update(currentCoordinateSystem);

      if (pose != nullptr)
      {
        m_registrationSystem->Update(currentCoordinateSystem, pose);
        m_gazeSystem->Update(m_timer, currentCoordinateSystem, pose);
        m_soundManager->Update(m_timer, currentCoordinateSystem);
        m_sliceRenderer->Update(pose, m_timer);
        m_notificationSystem->Update(pose, m_timer);
      }

      m_meshRenderer->Update(vp, m_timer, currentCoordinateSystem);
      m_modelRenderer->Update(m_timer, vp);
    });

    SetHolographicFocusPoint(prediction, holographicFrame, currentCoordinateSystem);

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  // Renders the current frame to each holographic camera, according to the
  // current application and spatial positioning state. Returns true if the
  // frame was rendered to at least one camera.
  bool HoloInterventionCore::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame)
  {
    if (m_timer.GetFrameCount() == 0)
    {
      return false;
    }

    // Lock the set of holographic camera resources, then draw to each camera in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool>(
             [this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap) -> bool
    {
      // Up-to-date frame predictions enhance the effectiveness of image stabilization and
      // allow more accurate positioning of holograms.
      holographicFrame->UpdateCurrentPrediction();
      HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

      SpatialCoordinateSystem^ currentCoordinateSystem =
      m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

      bool atLeastOneCameraRendered = false;
      for (auto cameraPose : prediction->CameraPoses)
      {
        // This represents the device-based resources for a HolographicCamera.
        DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

        // Get the device context.
        const auto context = m_deviceResources->GetD3DDeviceContext();
        const auto depthStencilView = pCameraResources->GetDepthStencilView();

        // Set render targets to the current holographic camera.
        ID3D11RenderTargetView* const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
        context->OMSetRenderTargets(1, targets, depthStencilView);

        // Clear the back buffer and depth stencil view.
        context->ClearRenderTargetView(targets[0], DirectX::Colors::Transparent);
        context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        DX::ViewProjection throwAway;
        pCameraResources->UpdateViewProjectionBuffer(m_deviceResources, cameraPose, currentCoordinateSystem, throwAway);
        bool activeCamera = pCameraResources->AttachViewProjectionBuffer(m_deviceResources);

        if (activeCamera)
        {
          m_meshRenderer->Render();
          m_modelRenderer->Render();
          m_sliceRenderer->Render();
          m_volumeRenderer->Render();
        }

        // Only render world-locked content when positional tracking is active.
        if (m_notificationSystem->IsShowingNotification())
        {
          m_notificationSystem->GetRenderer()->Render();
        }

        atLeastOneCameraRendered = true;
      }

      return atLeastOneCameraRendered;
    });
  }

  //----------------------------------------------------------------------------
  task<void> HoloInterventionCore::SaveAppStateAsync()
  {
    return m_spatialSystem->SaveAppStateAsync();
  }

  //----------------------------------------------------------------------------
  task<void> HoloInterventionCore::LoadAppStateAsync()
  {
    return m_spatialSystem->LoadAppStateAsync().then([ = ]()
    {
      // Registration must follow spatial due to anchor store
      m_registrationSystem->LoadAppStateAsync();
    });
  }

  //----------------------------------------------------------------------------
  uint64 HoloInterventionCore::GetCurrentFrameNumber() const
  {
    return m_timer.GetFrameCount();
  }

  //----------------------------------------------------------------------------
  System::NotificationSystem& HoloInterventionCore::GetNotificationsSystem()
  {
    return *m_notificationSystem.get();
  }

  //----------------------------------------------------------------------------
  System::SpatialSystem& HoloInterventionCore::GetSpatialSystem()
  {
    return *m_spatialSystem.get();
  }

  //----------------------------------------------------------------------------
  System::ToolSystem& HoloInterventionCore::GetToolSystem()
  {
    return *m_toolSystem.get();
  }

  //----------------------------------------------------------------------------
  System::GazeSystem& HoloInterventionCore::GetGazeSystem()
  {
    return *m_gazeSystem.get();
  }

  //----------------------------------------------------------------------------
  System::RegistrationSystem& HoloInterventionCore::GetRegistrationSystem()
  {
    return *m_registrationSystem.get();
  }

  //----------------------------------------------------------------------------
  System::ImagingSystem& HoloInterventionCore::GetImagingSystem()
  {
    return *m_imagingSystem.get();
  }

  //----------------------------------------------------------------------------
  Sound::SoundManager& HoloInterventionCore::GetSoundManager()
  {
    return *m_soundManager.get();
  }

  //----------------------------------------------------------------------------
  Network::IGTLinkIF& HoloInterventionCore::GetIGTLink()
  {
    return *m_igtLinkIF.get();
  }

  //----------------------------------------------------------------------------
  Rendering::ModelRenderer& HoloInterventionCore::GetModelRenderer()
  {
    return *m_modelRenderer.get();
  }

  //----------------------------------------------------------------------------
  Rendering::SliceRenderer& HoloInterventionCore::GetSliceRenderer()
  {
    return *m_sliceRenderer.get();
  }

  //----------------------------------------------------------------------------
  Rendering::VolumeRenderer& HoloInterventionCore::GetVolumeRenderer()
  {
    return *m_volumeRenderer.get();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnDeviceLost()
  {
    m_meshRenderer->ReleaseDeviceDependentResources();
    m_spatialSystem->ReleaseDeviceDependentResources();
    m_modelRenderer->ReleaseDeviceDependentResources();
    m_sliceRenderer->ReleaseDeviceDependentResources();
    m_notificationSystem->ReleaseDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnDeviceRestored()
  {
    m_meshRenderer->CreateDeviceDependentResources();
    m_modelRenderer->CreateDeviceDependentResources();
    m_sliceRenderer->CreateDeviceDependentResources();
    m_notificationSystem->CreateDeviceDependentResources();
    m_spatialSystem->CreateDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnLocatabilityChanged(SpatialLocator^ sender, Object^ args)
  {
    m_locatability = sender->Locatability;

    switch (sender->Locatability)
    {
    case SpatialLocatability::Unavailable:
    {
      m_notificationSystem->QueueMessage(L"Warning! Positional tracking is unavailable.");
    }
    break;

    case SpatialLocatability::PositionalTrackingActivating:
    case SpatialLocatability::OrientationOnly:
    case SpatialLocatability::PositionalTrackingInhibited:
      // Gaze-locked content still valid
      break;

    case SpatialLocatability::PositionalTrackingActive:
      m_notificationSystem->QueueMessage(L"Positional tracking is active.");
      break;
    }
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnCameraAdded(
    HolographicSpace^ sender,
    HolographicSpaceCameraAddedEventArgs^ args
  )
  {
    Deferral^ deferral = args->GetDeferral();
    HolographicCamera^ holographicCamera = args->Camera;
    create_task([this, deferral, holographicCamera]()
    {
      m_deviceResources->AddHolographicCamera(holographicCamera);

      // Holographic frame predictions will not include any information about this camera until the deferral is completed.
      deferral->Complete();
    });
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnCameraRemoved(
    HolographicSpace^ sender,
    HolographicSpaceCameraRemovedEventArgs^ args
  )
  {
    create_task([this]()
    {
      // TODO: Asynchronously unload or deactivate content resources (not back buffer
      //       resources) that are specific only to the camera that was removed.
    });

    m_deviceResources->RemoveHolographicCamera(args->Camera);
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::InitializeVoiceSystem()
  {
    Sound::VoiceInputCallbackMap callbacks;

    m_gazeSystem->RegisterVoiceCallbacks(callbacks);
    m_igtLinkIF->RegisterVoiceCallbacks(callbacks);
    m_spatialSystem->RegisterVoiceCallbacks(callbacks);
    m_toolSystem->RegisterVoiceCallbacks(callbacks);
    m_imagingSystem->RegisterVoiceCallbacks(callbacks);
    m_meshRenderer->RegisterVoiceCallbacks(callbacks);
    m_registrationSystem->RegisterVoiceCallbacks(callbacks);

    auto task = m_voiceInputHandler->CompileCallbacks(callbacks);
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicFocusPoint(HolographicFramePrediction^ prediction, HolographicFrame^ holographicFrame, SpatialCoordinateSystem^ currentCoordinateSystem)
  {
    for (auto cameraPose : prediction->CameraPoses)
    {
      HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters(cameraPose);

      float3 focusPointPosition = { 0.f, 0.f, 0.f };
      float3 focusPointNormal = { 0.f, 0.f, 0.f };
      float3 focusPointVelocity = { 0.f, 0.f, 0.f };

      if (m_notificationSystem->IsShowingNotification())
      {
        focusPointPosition = m_notificationSystem->GetPosition();
        focusPointNormal = (focusPointPosition == float3(0.f)) ? float3(0.f, 0.f, 1.f) : -normalize(focusPointPosition);
        focusPointVelocity = m_notificationSystem->GetVelocity();
      }
      else if (m_imagingSystem->HasSlice())
      {
        float4x4 mat = m_imagingSystem->GetSlicePose();

        SimpleMath::Matrix matrix;
        XMStoreFloat4x4(&matrix, XMMatrixTranspose(XMLoadFloat4x4(&mat)));
        SimpleMath::Vector3 translation;
        SimpleMath::Vector3 scale;
        SimpleMath::Quaternion rotation;
        matrix.Decompose(scale, rotation, translation);

        focusPointPosition = { translation.x, translation.y, translation.z };
        focusPointNormal = (focusPointPosition == float3(0.f)) ? float3(0.f, 0.f, 1.f) : -normalize(focusPointPosition);
        // TODO : store velocity of slice for stabilization?
        focusPointVelocity = float3(0.f);
      }
      else if (m_gazeSystem->IsCursorEnabled() && m_gazeSystem->GetHitNormal() != float3::zero())
      {
        focusPointPosition = m_gazeSystem->GetHitPosition();
        focusPointNormal = m_gazeSystem->GetHitNormal();
        focusPointVelocity = m_gazeSystem->GetHitVelocity();
      }

      if (focusPointNormal != float3::zero())
      {
        try
        {
          renderingParameters->SetFocusPoint(
            currentCoordinateSystem,
            focusPointPosition,
            focusPointNormal,
            focusPointVelocity
          );
        }
        catch (Platform::Exception^ ex)
        {
          m_notificationSystem->QueueMessage(ex->Message);
        }
      }
    }
  }
}