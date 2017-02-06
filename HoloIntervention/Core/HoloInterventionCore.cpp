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

// Local includes
#include "pch.h"
#include "AppView.h"
#include "Common.h"
#include "DirectXHelper.h"
#include "HoloInterventionCore.h"
#include "IEngineComponent.h"
#include "IStabilizedComponent.h"

// System includes
#include "GazeSystem.h"
#include "IconSystem.h"
#include "ImagingSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "SplashSystem.h"
#include "ToolSystem.h"

// Physics includes
#include "SurfaceAPI.h"

// Sound includes
#include "SoundAPI.h"

// Rendering includes
#include "ModelRenderer.h"
#include "NotificationRenderer.h"
#include "SliceRenderer.h"
#include "SpatialMeshRenderer.h"
#include "VolumeRenderer.h"

// Network includes
#include "IGTConnector.h"

// Input includes
#include "SpatialInput.h"
#include "VoiceInput.h"

// STL includes
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
    m_notificationRenderer = std::make_unique<Rendering::NotificationRenderer>(m_deviceResources);
    m_notificationSystem = std::make_unique<System::NotificationSystem>(*m_notificationRenderer.get());
    m_modelRenderer = std::make_unique<Rendering::ModelRenderer>(m_deviceResources);
    m_sliceRenderer = std::make_unique<Rendering::SliceRenderer>(m_deviceResources);
    m_volumeRenderer = std::make_unique<Rendering::VolumeRenderer>(m_deviceResources);
    m_meshRenderer = std::make_unique<Rendering::SpatialMeshRenderer>(*m_notificationSystem.get(), m_deviceResources);

    m_soundAPI = std::make_unique<Sound::SoundAPI>();

    m_spatialInput = std::make_unique<Input::SpatialInput>();
    m_voiceInput = std::make_unique<Input::VoiceInput>(*m_notificationSystem.get(), *m_soundAPI.get());

    m_IGTConnector = std::make_unique<Network::IGTConnector>(*m_notificationSystem.get());
    HoloIntervention::Log::instance().SetIGTConnector(*m_IGTConnector.get());
    m_physicsAPI = std::make_unique<Physics::SurfaceAPI>(*m_notificationSystem.get(), m_deviceResources, m_timer);

    m_registrationSystem = std::make_unique<System::RegistrationSystem>(*m_IGTConnector.get(), *m_physicsAPI.get(), *m_notificationSystem.get(), *m_modelRenderer.get());
    m_iconSystem = std::make_unique<System::IconSystem>(*m_notificationSystem.get(), *m_registrationSystem.get(), *m_IGTConnector.get(), *m_modelRenderer.get());
    m_gazeSystem = std::make_unique<System::GazeSystem>(*m_notificationSystem.get(), *m_physicsAPI.get(), *m_modelRenderer.get());
    m_toolSystem = std::make_unique<System::ToolSystem>(*m_notificationSystem.get(), *m_registrationSystem.get(), *m_modelRenderer.get());
    m_imagingSystem = std::make_unique<System::ImagingSystem>(*m_registrationSystem.get(), *m_notificationSystem.get(), *m_sliceRenderer.get(), *m_volumeRenderer.get());
    m_splashSystem = std::make_unique<System::SplashSystem>(*m_sliceRenderer.get());

    m_engineComponents.push_back(m_modelRenderer.get());
    m_engineComponents.push_back(m_sliceRenderer.get());
    m_engineComponents.push_back(m_volumeRenderer.get());
    m_engineComponents.push_back(m_meshRenderer.get());
    m_engineComponents.push_back(m_soundAPI.get());
    m_engineComponents.push_back(m_notificationSystem.get());
    m_engineComponents.push_back(m_spatialInput.get());
    m_engineComponents.push_back(m_voiceInput.get());
    m_engineComponents.push_back(m_physicsAPI.get());
    m_engineComponents.push_back(m_IGTConnector.get());
    m_engineComponents.push_back(m_gazeSystem.get());
    m_engineComponents.push_back(m_toolSystem.get());
    m_engineComponents.push_back(m_registrationSystem.get());
    m_engineComponents.push_back(m_imagingSystem.get());
    m_engineComponents.push_back(m_iconSystem.get());
    m_engineComponents.push_back(m_splashSystem.get());

    // TODO : remove temp code
    m_IGTConnector->SetHostname(L"192.168.0.103");

    try
    {
      m_soundAPI->InitializeAsync();
    }
    catch (Platform::Exception^ e)
    {
      m_notificationSystem->QueueMessage(L"Unable to initialize audio system. See log.");
      OutputDebugStringW((L"Audio Error" + e->Message)->Data());
    }

    InitializeVoiceSystem();

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    m_locatabilityChangedToken = m_locator->LocatabilityChanged +=
                                   ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^>(std::bind(&HoloInterventionCore::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraAddedToken = m_holographicSpace->CameraAdded +=
                           ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^>(std::bind(&HoloInterventionCore::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraRemovedToken = m_holographicSpace->CameraRemoved +=
                             ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^>(std::bind(&HoloInterventionCore::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2));

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();

    // Initialize the notification system with a bogus frame to grab sensor data
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(holographicFrame->CurrentPrediction->Timestamp);
    SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp(currentCoordinateSystem, holographicFrame->CurrentPrediction->Timestamp);
    m_notificationSystem->Initialize(pose);
    m_physicsAPI->InitializeSurfaceObserverAsync(currentCoordinateSystem).then([this](bool result)
    {
      if (!result)
      {
        // TODO : add more robust error handling
        m_notificationSystem->QueueMessage("Unable to initialize surface observer.");
      }
    });
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::UnregisterHolographicEventHandlers()
  {
    if (m_holographicSpace != nullptr)
    {
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

    SpatialCoordinateSystem^ hmdCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

    DX::CameraResources* cameraResources(nullptr);
    m_deviceResources->UseHolographicCameraResources<bool>([this, holographicFrame, prediction, hmdCoordinateSystem, &cameraResources](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
    {
      for (auto cameraPose : prediction->CameraPoses)
      {
        cameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();
        auto result = cameraResources->Update(m_deviceResources, cameraPose, hmdCoordinateSystem);
      }
      return true;
    });

    if (!m_engineReady)
    {
      m_engineReady = true;
      for (auto& component : m_engineComponents)
      {
        m_engineReady = m_engineReady && component->IsReady();
      }
    }

    if (m_engineReady && !m_voiceInput->IsVoiceEnabled())
    {
      m_voiceInput->EnableVoiceAnalysis(true);
    }

    // Time-based updates
    m_timer.Tick([&]()
    {
      SpatialPointerPose^ headPose = SpatialPointerPose::TryGetAtTimestamp(hmdCoordinateSystem, prediction->Timestamp);

      if (!m_engineReady)
      {
        // Show our welcome screen until it is ready!
        m_splashSystem->Update(m_timer, hmdCoordinateSystem, headPose);
        m_sliceRenderer->Update(headPose, m_timer);
      }
      else
      {
        if (m_IGTConnector->IsConnected())
        {
          if (m_IGTConnector->GetTrackedFrame(m_latestFrame, &m_latestTimestamp))
          {
            m_latestTimestamp = m_latestFrame->Timestamp;
            // TODO : extract system logic from volume renderer and move to imaging system
            if (headPose != nullptr)
            {
              m_volumeRenderer->Update(m_latestFrame, m_timer, cameraResources, hmdCoordinateSystem, headPose);
            }
            m_imagingSystem->Update(m_latestFrame, m_timer, hmdCoordinateSystem);
            m_toolSystem->Update(m_latestFrame, m_timer, hmdCoordinateSystem);
          }
        }

        m_physicsAPI->Update(hmdCoordinateSystem);

        if (headPose != nullptr)
        {
          m_registrationSystem->Update(m_timer, hmdCoordinateSystem, headPose);
          m_gazeSystem->Update(m_timer, hmdCoordinateSystem, headPose);
          m_iconSystem->Update(m_timer, headPose);
          m_soundAPI->Update(m_timer, hmdCoordinateSystem);
          m_sliceRenderer->Update(headPose, m_timer);
          m_notificationSystem->Update(headPose, m_timer);
        }

        m_meshRenderer->Update(m_timer, hmdCoordinateSystem);
        m_modelRenderer->Update(m_timer, cameraResources);
      }
    });

    SetHolographicFocusPoint(prediction, holographicFrame, hmdCoordinateSystem);

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  bool HoloInterventionCore::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame)
  {
    if (m_timer.GetFrameCount() == 0)
    {
      return false;
    }

    // Lock the set of holographic camera resources, then draw to each camera in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool>([this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap) -> bool
    {
      holographicFrame->UpdateCurrentPrediction();
      HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

      SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

      bool atLeastOneCameraRendered = false;
      for (auto cameraPose : prediction->CameraPoses)
      {
        DX::CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

        const auto context = m_deviceResources->GetD3DDeviceContext();
        const auto depthStencilView = pCameraResources->GetDepthStencilView();

        ID3D11RenderTargetView* const targets[1] = { pCameraResources->GetBackBufferRenderTargetView() };
        context->OMSetRenderTargets(1, targets, depthStencilView);

        context->ClearRenderTargetView(targets[0], DirectX::Colors::Transparent);
        context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        pCameraResources->Update(m_deviceResources, cameraPose, currentCoordinateSystem);
        bool activeCamera = pCameraResources->Attach(m_deviceResources);

        if (activeCamera)
        {
          if (m_engineReady)
          {
            m_meshRenderer->Render();
            m_modelRenderer->Render();
            m_sliceRenderer->Render();
            m_volumeRenderer->Render();

            if (m_notificationSystem->IsShowingNotification())
            {
              m_notificationRenderer->Render();
            }
          }
          else
          {
            // Show our welcome screen until it is ready!
            m_sliceRenderer->Render();
          }

          atLeastOneCameraRendered = true;
        }
      }

      return atLeastOneCameraRendered;
    });
  }

  //----------------------------------------------------------------------------
  task<void> HoloInterventionCore::SaveAppStateAsync()
  {
    return m_physicsAPI->SaveAppStateAsync();
  }

  //----------------------------------------------------------------------------
  task<void> HoloInterventionCore::LoadAppStateAsync()
  {
    return m_physicsAPI->LoadAppStateAsync().then([ = ]()
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
  Physics::SurfaceAPI& HoloInterventionCore::GetSurfaceAPI()
  {
    return *m_physicsAPI.get();
  }

  //----------------------------------------------------------------------------
  System::ToolSystem& HoloInterventionCore::GetToolSystem()
  {
    return *m_toolSystem.get();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::System::SplashSystem& HoloInterventionCore::GetSplashSystem()
  {
    return *m_splashSystem.get();
  }

  //----------------------------------------------------------------------------
  System::IconSystem& HoloInterventionCore::GetIconSystem()
  {
    return *m_iconSystem.get();
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
  Sound::SoundAPI& HoloInterventionCore::GetSoundAPI()
  {
    return *m_soundAPI.get();
  }

  //----------------------------------------------------------------------------
  Network::IGTConnector& HoloInterventionCore::GetIGTLink()
  {
    return *m_IGTConnector.get();
  }

  //----------------------------------------------------------------------------
  bool HoloInterventionCore::HasModelRenderer() const
  {
    return m_modelRenderer != nullptr;
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
    m_physicsAPI->ReleaseDeviceDependentResources();
    m_modelRenderer->ReleaseDeviceDependentResources();
    m_sliceRenderer->ReleaseDeviceDependentResources();
    m_notificationRenderer->ReleaseDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnDeviceRestored()
  {
    m_notificationRenderer->CreateDeviceDependentResourcesAsync();
    m_meshRenderer->CreateDeviceDependentResources();
    m_modelRenderer->CreateDeviceDependentResources();
    m_sliceRenderer->CreateDeviceDependentResources();
    m_physicsAPI->CreateDeviceDependentResources();
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
    m_IGTConnector->RegisterVoiceCallbacks(callbacks);
    m_physicsAPI->RegisterVoiceCallbacks(callbacks);
    m_toolSystem->RegisterVoiceCallbacks(callbacks);
    m_imagingSystem->RegisterVoiceCallbacks(callbacks);
    m_meshRenderer->RegisterVoiceCallbacks(callbacks);
    m_registrationSystem->RegisterVoiceCallbacks(callbacks);

    auto task = m_voiceInput->CompileCallbacks(callbacks);
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicFocusPoint(HolographicFramePrediction^ prediction, HolographicFrame^ holographicFrame, SpatialCoordinateSystem^ currentCoordinateSystem)
  {
    float maxPriority(-1.f);
    IStabilizedComponent* winningComponent(nullptr);
    for (auto& component : m_engineComponents)
    {
      IStabilizedComponent* stabilizedComponent = dynamic_cast<IStabilizedComponent*>(component);
      if (stabilizedComponent != nullptr && stabilizedComponent->GetStabilizePriority() > maxPriority)
      {
        maxPriority = stabilizedComponent->GetStabilizePriority();
        winningComponent = stabilizedComponent;
      }
    }

    if (winningComponent == nullptr)
    {
      HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, "No component returned a stabilization request.");
      return;
    }

    for (auto cameraPose : prediction->CameraPoses)
    {
      HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters(cameraPose);

      float3 focusPointPosition = winningComponent->GetStabilizedPosition();
      float3 focusPointNormal = winningComponent->GetStabilizedNormal();
      float3 focusPointVelocity = winningComponent->GetStabilizedVelocity();

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
        catch (Platform::Exception^ e)
        {
          HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
        }
      }
    }
  }
}