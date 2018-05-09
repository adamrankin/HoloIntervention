/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Debug.h"
#include "DirectXHelper.h"
#include "HoloInterventionCore.h"
#include "IConfigurable.h"
#include "IEngineComponent.h"
#include "ILocatable.h"
#include "IStabilizedComponent.h"

// System includes
#include "GazeSystem.h"
#include "ImagingSystem.h"
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "SplashSystem.h"
#include "TaskSystem.h"
#include "ToolSystem.h"

// UI includes
#include "Icons.h"

// Physics includes
#include "PhysicsAPI.h"

// Sound includes
#include "SoundAPI.h"

// Rendering includes
#include "ModelRenderer.h"
#include "NotificationRenderer.h"
#include "SliceRenderer.h"
#include "MeshRenderer.h"
#include "VolumeRenderer.h"

// Input includes
#include "SpatialInput.h"
#include "VoiceInput.h"

// STL includes
#include <iomanip>
#include <regex>
#include <string>

// Windows includes
#include <windows.graphics.directx.direct3d11.interop.h>

// Intellisense includes
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::System::Threading;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  // Loads and initializes application assets when the application is loaded.
  HoloInterventionCore::HoloInterventionCore(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
  {
    m_deviceResources->RegisterDeviceNotify(this);
  }

  //----------------------------------------------------------------------------
  HoloInterventionCore::~HoloInterventionCore()
  {
    m_deviceResources->RegisterDeviceNotify(nullptr);
    UnregisterHolographicEventHandlers();

    if (m_locatabilityIcon != nullptr)
    {
      m_icons->RemoveEntry(m_locatabilityIcon->GetId());
    }
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicSpace(HolographicSpace^ holographicSpace)
  {
    UnregisterHolographicEventHandlers();

    m_holographicSpace = holographicSpace;

    // Engine components
    m_debug = std::make_unique<Debug>(*m_sliceRenderer.get(), m_deviceResources);
    m_modelRenderer = std::make_unique<Rendering::ModelRenderer>(m_deviceResources, m_timer, *m_debug.get());
    m_sliceRenderer = std::make_unique<Rendering::SliceRenderer>(m_deviceResources, m_timer, *m_debug.get());
    m_debug->SetModelRenderer(m_modelRenderer.get());
    m_debug->SetSliceRenderer(m_sliceRenderer.get());

    m_notificationRenderer = std::make_unique<Rendering::NotificationRenderer>(m_deviceResources);
    m_volumeRenderer = std::make_unique<Rendering::VolumeRenderer> (m_deviceResources, m_timer);
    m_physicsAPI = std::make_unique<Physics::PhysicsAPI>(m_deviceResources, m_timer);
    m_meshRenderer = std::make_unique<Rendering::MeshRenderer> (m_deviceResources, *m_physicsAPI);
    m_icons = std::make_unique<UI::Icons>(*m_modelRenderer.get());
    m_soundAPI = std::make_unique<Sound::SoundAPI>();
    m_spatialInput = std::make_unique<Input::SpatialInput>();
    m_voiceInput = std::make_unique<Input::VoiceInput> (*m_soundAPI.get(), *m_icons.get());

    m_icons->AddEntryAsync(L"satellite.cmo", L"satellite").then([this](std::shared_ptr<UI::Icon> entry)
    {
      m_locatabilityIcon = entry;
      entry->SetUserRotation(Math::PI_2<float>, Math::PI<float>, 0.0);
      entry->GetModel()->RenderDefault();
    });

    // Systems (apps-specific behaviour), eventually move to separate project and have engine DLL
    m_notificationSystem = std::make_unique<System::NotificationSystem>(*m_notificationRenderer.get());
    m_networkSystem = std::make_unique<System::NetworkSystem> (*m_notificationSystem.get(), *m_voiceInput.get(), *m_icons.get(), *m_debug.get());
    m_registrationSystem = std::make_unique<System::RegistrationSystem>(*this, *m_networkSystem.get(), *m_physicsAPI.get(), *m_notificationSystem.get(), *m_modelRenderer.get(), *m_spatialInput.get(), *m_icons.get(), *m_debug.get(), m_timer);
    m_toolSystem = std::make_unique<System::ToolSystem>(*m_notificationSystem.get(), *m_registrationSystem.get(), *m_modelRenderer.get(), *m_networkSystem.get(), *m_icons.get());
    m_gazeSystem = std::make_unique<System::GazeSystem> (*m_notificationSystem.get(), *m_physicsAPI.get(), *m_modelRenderer.get());
    m_imagingSystem = std::make_unique<System::ImagingSystem> (*m_registrationSystem.get(), *m_notificationSystem.get(), *m_sliceRenderer.get(), *m_volumeRenderer.get(), *m_networkSystem.get(), *m_debug.get());
    m_splashSystem = std::make_unique<System::SplashSystem> (*m_sliceRenderer.get());
    m_taskSystem = std::make_unique<System::TaskSystem> (*m_notificationSystem.get(), *m_networkSystem.get(), *m_toolSystem.get(), *m_registrationSystem.get(), *m_modelRenderer.get(), *m_icons.get());

    m_engineComponents.push_back(m_modelRenderer.get());
    m_engineComponents.push_back(m_sliceRenderer.get());
    m_engineComponents.push_back(m_volumeRenderer.get());
    m_engineComponents.push_back(m_meshRenderer.get());
    m_engineComponents.push_back(m_soundAPI.get());
    m_engineComponents.push_back(m_notificationSystem.get());
    m_engineComponents.push_back(m_spatialInput.get());
    m_engineComponents.push_back(m_voiceInput.get());
    m_engineComponents.push_back(m_physicsAPI.get());
    m_engineComponents.push_back(m_icons.get());

    m_engineComponents.push_back(m_networkSystem.get());
    m_engineComponents.push_back(m_gazeSystem.get());
    m_engineComponents.push_back(m_toolSystem.get());
    m_engineComponents.push_back(m_registrationSystem.get());
    m_engineComponents.push_back(m_imagingSystem.get());
    m_engineComponents.push_back(m_splashSystem.get());
    m_engineComponents.push_back(m_taskSystem.get());

    m_configurables.push_back(m_toolSystem.get());
    m_configurables.push_back(m_registrationSystem.get());
    m_configurables.push_back(m_networkSystem.get());
    m_configurables.push_back(m_imagingSystem.get());
    m_configurables.push_back(m_taskSystem.get());

    ReadConfigurationAsync().then([this](bool result)
    {
      if (!result)
      {
        LOG_ERROR("Unable to initialize system. Loading of configuration failed.");
        Log::instance().EndSessionAsync();
      }

      RegisterVoiceCallbacks();
    });

    m_soundAPI->InitializeAsync().then([this](task<HRESULT> initTask)
    {
      HRESULT hr(S_OK);
      try
      {
        hr = initTask.get();
      }
      catch (Platform::Exception^ e)
      {
        m_notificationSystem->QueueMessage(L"Unable to initialize audio system. See log.");
        OutputDebugStringW((L"Audio Error" + e->Message)->Data());
      }
    });

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    for (auto& system : m_locatables)
    {
      system->OnLocatabilityChanged(m_locator->Locatability);
    }

    m_locatabilityChangedToken = m_locator->LocatabilityChanged +=
                                   ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^> (std::bind(&HoloInterventionCore::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraAddedToken = m_holographicSpace->CameraAdded +=
                           ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^> (std::bind(&HoloInterventionCore::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraRemovedToken = m_holographicSpace->CameraRemoved +=
                             ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^> (std::bind(&HoloInterventionCore::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2));

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();

    // Initialize the notification system with a bogus frame to grab sensor data
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(holographicFrame->CurrentPrediction->Timestamp);
    SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp(currentCoordinateSystem, holographicFrame->CurrentPrediction->Timestamp);
    m_notificationSystem->Initialize(pose);
    m_physicsAPI->InitializeSurfaceObserverAsync(currentCoordinateSystem).then([this](task<bool> initTask)
    {
      bool result(false);
      try
      {
        result = initTask.get();
      }
      catch (const std::exception&)
      {
        LOG_ERROR("Unable to initialize surface observers. Mesh data not available.");
        result = false;
      }

      if (!result)
      {
        // TODO : add more robust error handling
        m_notificationSystem->QueueMessage("Unable to initialize surface observer.");
      }

      LoadAppStateAsync().then([this](bool result)
      {
        if (!result)
        {
          LOG_ERROR("Unable to load app state. Starting new session.");
        }

        LOG(LogLevelType::LOG_LEVEL_INFO, "Engine loading...");
        uint32 componentsReady(0);
        bool engineReady(true);

        auto messageId = m_notificationSystem->QueueMessage(L"Loading ... 0%");
        double lastValue = 0.0;
        do
        {
          componentsReady = 0;
          engineReady = true;

          std::this_thread::sleep_for(std::chrono::milliseconds(16));

          for (auto& component : m_engineComponents)
          {
#if defined(_DEBUG)
            if (!component->IsReady())
            {
              std::string name(typeid(*component).name());
              std::wstring wname(name.begin(), name.end());
              m_debug->UpdateValue(L"not-ready-comp", wname);
            }
#endif
            engineReady = engineReady && component->IsReady();
            if (component->IsReady())
            {
              componentsReady++;
            }
          }

          if ((double)componentsReady / m_engineComponents.size() * 100 == lastValue)
          {
            continue;
          }
          lastValue = (double)componentsReady / m_engineComponents.size() * 100;

          m_notificationSystem->RemoveMessage(messageId);
          std::wstringstream wss;
          wss << L"Loading ... " << std::fixed << std::setprecision(1) << lastValue << L"%";
          messageId = m_notificationSystem->QueueMessage(wss.str());
        }
        while (!engineReady);

        m_engineReady = true;
      }).then([this]()
      {
        LOG(LogLevelType::LOG_LEVEL_INFO, "Engine loaded.");

        m_splashSystem->EndSplash();
        m_voiceInput->EnableVoiceAnalysis(true);
      });
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
    if (!m_engineUserEnabled)
    {
      return nullptr;
    }

    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

    SpatialCoordinateSystem^ hmdCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

    DX::CameraResources* cameraResources(nullptr);
    if (!m_deviceResources->UseHolographicCameraResources<bool>([this, holographicFrame, prediction, hmdCoordinateSystem, &cameraResources](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
  {
    for (auto cameraPose : prediction->CameraPoses)
      {
        cameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();
        if (cameraResources == nullptr)
        {
          return false;
        }
        auto result = cameraResources->Update(m_deviceResources, cameraPose, hmdCoordinateSystem);
      }
      return true;
    }))
    {
      // Camera resources failed
      LOG_ERROR("Camera update failed. Skipping frame.");
      return nullptr;
    }

    // Time-based updates
    m_timer.Tick([&]()
    {
      SpatialPointerPose^ headPose = SpatialPointerPose::TryGetAtTimestamp(hmdCoordinateSystem, prediction->Timestamp);

      if (!m_engineReady)
      {
        // Show our welcome screen until the engine is ready!
        m_splashSystem->Update(m_timer, hmdCoordinateSystem, headPose);
        m_sliceRenderer->Update(headPose, cameraResources);
        m_notificationSystem->Update(headPose, m_timer);
      }
      else
      {
        m_voiceInput->Update(m_timer);

        if (headPose != nullptr)
        {
          m_volumeRenderer->Update(cameraResources, hmdCoordinateSystem, headPose);
        }
        m_imagingSystem->Update(m_timer, hmdCoordinateSystem);
        m_toolSystem->Update(m_timer, hmdCoordinateSystem);
        m_networkSystem->Update(m_timer);
        m_taskSystem->Update(hmdCoordinateSystem, m_timer);

        m_physicsAPI->Update(hmdCoordinateSystem);

        if (headPose != nullptr)
        {
          m_registrationSystem->Update(hmdCoordinateSystem, headPose, prediction->CameraPoses->GetAt(0));
          m_gazeSystem->Update(m_timer, hmdCoordinateSystem, headPose);
          m_icons->Update(m_timer, headPose);
          m_soundAPI->Update(m_timer, hmdCoordinateSystem);
          m_sliceRenderer->Update(headPose, cameraResources);
          m_notificationSystem->Update(headPose, m_timer);
        }

        m_modelRenderer->Update(cameraResources);
      }

      m_debug->Update(hmdCoordinateSystem);
    });

    SpatialPointerPose^ headPose = SpatialPointerPose::TryGetAtTimestamp(hmdCoordinateSystem, prediction->Timestamp);
    SetHolographicFocusPoint(prediction, holographicFrame, hmdCoordinateSystem, headPose);

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  bool HoloInterventionCore::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame)
  {
    if (m_timer.GetFrameCount() == 0 || !m_engineUserEnabled || holographicFrame == nullptr)
    {
      return false;
    }

    // Lock the set of holographic camera resources, then draw to each camera in this frame.
    return m_deviceResources->UseHolographicCameraResources<bool> ([this, holographicFrame](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap) -> bool
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
            if (m_notificationSystem->IsShowingNotification())
            {
              m_notificationRenderer->Render();
            }
          }

          atLeastOneCameraRendered = true;
        }
      }

      return atLeastOneCameraRendered;
    });
  }

  //----------------------------------------------------------------------------
  task<bool> HoloInterventionCore::SaveAppStateAsync()
  {
    if (m_physicsAPI == nullptr)
    {
      return task_from_result(false);
    }
    return create_task([this]()
    {
      uint32 timer(0);
      while (!m_physicsAPI->IsReady() && timer < 5000) //(wait for 5s)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timer += 100;
      }
      if (timer >= 5000)
      {
        return task_from_result(false);
      }
      return m_physicsAPI->SaveAppStateAsync();
    });
  }

  //----------------------------------------------------------------------------
  task<bool> HoloInterventionCore::LoadAppStateAsync()
  {
    return create_task([this]()
    {
      while (m_physicsAPI == nullptr || m_registrationSystem == nullptr || !m_physicsAPI->IsReady() || !m_registrationSystem->IsReady())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      return m_physicsAPI->LoadAppStateAsync().then([this](bool result)
      {
        if (!result)
        {
          return task_from_result(result);
        }
        // Registration must follow spatial due to anchor store
        return m_registrationSystem->LoadAppStateAsync();
      });
    });
  }

  //----------------------------------------------------------------------------
  uint64 HoloInterventionCore::GetCurrentFrameNumber() const
  {
    return m_timer.GetFrameCount();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnDeviceLost()
  {
    m_meshRenderer->ReleaseDeviceDependentResources();
    m_physicsAPI->ReleaseDeviceDependentResources();
    m_modelRenderer->ReleaseDeviceDependentResources();
    m_sliceRenderer->ReleaseDeviceDependentResources();
    m_notificationRenderer->ReleaseDeviceDependentResources();
    m_volumeRenderer->ReleaseDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnDeviceRestored()
  {
    m_notificationRenderer->CreateDeviceDependentResourcesAsync();
    m_meshRenderer->CreateDeviceDependentResources();
    m_modelRenderer->CreateDeviceDependentResources();
    m_sliceRenderer->CreateDeviceDependentResources();
    m_volumeRenderer->CreateDeviceDependentResourcesAsync();
    m_physicsAPI->CreateDeviceDependentResourcesAsync();
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::RegisterLocatable(ILocatable* locatable)
  {
    if (std::find(begin(m_locatables), end(m_locatables), locatable) == end(m_locatables))
    {
      m_locatables.push_back(locatable);
    }
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::UnregisterLocatable(ILocatable* locatable)
  {
    std::vector<ILocatable*>::iterator it = std::find(begin(m_locatables), end(m_locatables), locatable);
    if (it != end(m_locatables))
    {
      m_locatables.erase(it);
    }
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::OnLocatabilityChanged(SpatialLocator^ sender, Object^ args)
  {
    m_locatability = sender->Locatability;

    for (auto& locatable : m_locatables)
    {
      locatable->OnLocatabilityChanged(sender->Locatability);
    }

    if (m_locatabilityIcon == nullptr)
    {
      return;
    }

    switch (sender->Locatability)
    {
    case SpatialLocatability::Unavailable:
    {
      m_locatabilityIcon->GetModel()->SetColour(1.0, 0.0, 0.0);
      m_notificationSystem->QueueMessage(L"Warning! Positional tracking is unavailable.");
    }
    break;

    case SpatialLocatability::PositionalTrackingActivating:
    case SpatialLocatability::OrientationOnly:
    case SpatialLocatability::PositionalTrackingInhibited:
      // Gaze-locked content still valid
      m_locatabilityIcon->GetModel()->SetColour(1.0, 1.0, 0.0);
      break;

    case SpatialLocatability::PositionalTrackingActive:
      m_locatabilityIcon->GetModel()->RenderDefault();
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
  void HoloInterventionCore::RegisterVoiceCallbacks()
  {
    Input::VoiceInputCallbackMap callbacks;

    m_debug->RegisterVoiceCallbacks(callbacks);
    m_gazeSystem->RegisterVoiceCallbacks(callbacks);
    m_networkSystem->RegisterVoiceCallbacks(callbacks);
    m_physicsAPI->RegisterVoiceCallbacks(callbacks);
    m_toolSystem->RegisterVoiceCallbacks(callbacks);
    m_imagingSystem->RegisterVoiceCallbacks(callbacks);
    m_registrationSystem->RegisterVoiceCallbacks(callbacks);
    m_taskSystem->RegisterVoiceCallbacks(callbacks);
    m_meshRenderer->RegisterVoiceCallbacks(callbacks);

    callbacks[L"end session"] = [this](SpeechRecognitionResult ^ result)
    {
      Log::instance().EndSessionAsync();
      m_notificationSystem->QueueMessage(L"Log session ended.");
    };

    callbacks[L"save config"] = [this](SpeechRecognitionResult ^ result)
    {
      m_physicsAPI->SaveAppStateAsync().then([this](bool result)
      {
        if (!result)
        {
          // Physics API didn't save
          m_notificationSystem->QueueMessage("Unable to save anchors. Continuing.");
        }

        WriteConfigurationAsync().then([this](bool result)
        {
          if (result)
          {
            m_notificationSystem->QueueMessage(L"Save successful.");
          }
          else
          {
            m_notificationSystem->QueueMessage(L"Save failed.");
          }
        });
      });
    };

    callbacks[L"hide all"] = [this](SpeechRecognitionResult ^ result)
    {
      m_engineUserEnabled = false;
    };

    callbacks[L"show all"] = [this](SpeechRecognitionResult ^ result)
    {
      m_engineUserEnabled = true;
    };

    m_voiceInput->CompileCallbacksAsync(callbacks).then([this](task<bool> compileTask)
    {
      bool result;
      try
      {
        result = compileTask.get();
        m_voiceInput->SwitchToCommandRecognitionAsync();
      }
      catch (const std::exception& e)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Failed to compile voice callbacks: ") + e.what());
        m_notificationSystem->QueueMessage(L"Unable to initialize voice input system. Critical failure.");
      }
    });
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicFocusPoint(HolographicFramePrediction^ prediction,
      HolographicFrame^ holographicFrame,
      SpatialCoordinateSystem^ currentCoordinateSystem,
      SpatialPointerPose^ pose)
  {
    float maxPriority(PRIORITY_NOT_ACTIVE);
    IStabilizedComponent* winningComponent(nullptr);
    for (auto component : m_engineComponents)
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
      LOG(LogLevelType::LOG_LEVEL_WARNING, "No component returned a stabilization request.");
      return;
    }

    for (auto cameraPose : prediction->CameraPoses)
    {
      HolographicCameraRenderingParameters^ renderingParameters = holographicFrame->GetRenderingParameters(cameraPose);

      float3 focusPointPosition = winningComponent->GetStabilizedPosition(pose);
      float3 focusPointVelocity = winningComponent->GetStabilizedVelocity();

#if defined(_DEBUG)
      std::string className(typeid(*winningComponent).name());
      std::wstring wClassName(begin(className), end(className));
      m_debug->UpdateValue(L"WinComp", wClassName);
#endif

      try
      {
        renderingParameters->SetFocusPoint(
          currentCoordinateSystem,
          focusPointPosition,
          -pose->Head->ForwardDirection,
          focusPointVelocity
        );
      }
      catch (Platform::Exception^ e)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, e->Message);
      }
    }
  }

  //----------------------------------------------------------------------------
  task<bool> HoloInterventionCore::WriteConfigurationAsync()
  {
    // Backup current file
    return create_task(ApplicationData::Current->LocalFolder->TryGetItemAsync(L"configuration.xml")).then([this](IStorageItem ^ item)
    {
      if (item == nullptr)
      {
        return task_from_result((StorageFile^) nullptr);
      }
      else
      {
        StorageFile^ file = dynamic_cast<StorageFile^>(item);
        auto Calendar = ref new Windows::Globalization::Calendar();
        Calendar->SetToNow();
        auto fileName = L"configuration_" + Calendar->YearAsString() + L"-" + Calendar->MonthAsNumericString() + L"-" + Calendar->DayAsString() + L"T" + Calendar->HourAsPaddedString(2) + L"h" + Calendar->MinuteAsPaddedString(2) + L"m" + Calendar->SecondAsPaddedString(2) + L"s.xml";

        return create_task(file->CopyAsync(ApplicationData::Current->LocalFolder, fileName, Windows::Storage::NameCollisionOption::GenerateUniqueName));
      }
    }).then([this](task<StorageFile^> copyTask)
    {
      try
      {
        copyTask.wait();
      }
      catch (Platform::Exception^ e)
      {
        WLOG_ERROR(L"Unable to backup existing configuration. Data loss may occur. Error: " + e->Message);
      }

      // Create new document
      auto doc = ref new XmlDocument();

      // Add root node
      auto elem = doc->CreateElement(L"HoloIntervention");
      doc->AppendChild(elem);

      auto docElem = doc->DocumentElement;

      // Populate document
      std::vector<task<bool>> tasks;
      for (auto comp : m_configurables)
      {
        tasks.push_back(comp->WriteConfigurationAsync(doc));
      }

      return when_all(begin(tasks), end(tasks)).then([this, doc](std::vector<bool> results)
      {
        // Write document to disk
        return create_task(ApplicationData::Current->LocalFolder->CreateFileAsync(L"configuration.xml", CreationCollisionOption::ReplaceExisting)).then([this, doc](StorageFile ^ file)
        {
          auto xmlAsString = std::wstring(doc->GetXml()->Data());

          // Custom formatting
          // After every > add two \r\n
          xmlAsString = std::regex_replace(xmlAsString, std::wregex(L">"), L">\r\n\r\n");
          xmlAsString = std::regex_replace(xmlAsString, std::wregex(L"\" "), L"\"\r\n  ");
          xmlAsString.insert(0, L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n");

          return create_task(Windows::Storage::FileIO::WriteTextAsync(file, ref new Platform::String(xmlAsString.c_str()))).then([this](task<void> writeTask)
          {
            try
            {
              writeTask.wait();
            }
            catch (const std::exception& e)
            {
              LOG_ERROR(std::string("Unable to write to file: ") + e.what());
              return false;
            }

            return true;
          });
        });
      });
    });
  }

  //----------------------------------------------------------------------------
  task<bool> HoloInterventionCore::ReadConfigurationAsync()
  {
    if (m_configurables.size() == 0)
    {
      return task_from_result(true);
    }

    return create_task(ApplicationData::Current->LocalFolder->TryGetItemAsync(L"configuration.xml")).then([this](IStorageItem ^ file)
    {
      if (file == nullptr)
      {
        // If the user specific file doesn't exist, copy the default from the installed location
        return create_task(Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Data\\configuration.xml")).then([this](task<StorageFile^> getTask)
        {
          StorageFile^ file;
          try
          {
            file = getTask.get();
          }
          catch (Platform::Exception^)
          {
            // Not local, not installed... what happened!?
            assert(false);
          }

          return create_task(file->CopyAsync(ApplicationData::Current->LocalFolder, L"configuration.xml")).then([this](task<StorageFile^> copyTask)
          {
            try
            {
              return copyTask.get();
            }
            catch (Platform::Exception^)
            {
              return (StorageFile^) nullptr;
            }
          });
        });
      }
      else
      {
        return task_from_result(dynamic_cast<StorageFile^>(file));
      }
    }).then([this](task<StorageFile^> fileTask)
    {
      StorageFile^ file;
      try
      {
        file = fileTask.get();
      }
      catch (Platform::Exception^)
      {
        // Not local, not installed... what happened!?
        task_from_result(false);
      }

      if (file == nullptr)
      {
        return task_from_result(false);
      }

      return LoadXmlDocumentAsync(file).then([this](task<XmlDocument^> docTask)
      {
        XmlDocument^ doc;
        try
        {
          doc = docTask.get();
        }
        catch (Platform::Exception^)
        {
          // Not local, not installed... what happened!?
          return task_from_result(false);
        }

        // Read application level configuration
        Platform::String^ xpath = L"/HoloIntervention";
        if (doc->SelectNodes(xpath)->Length != 1)
        {
          // No configuration found, use defaults
          LOG_ERROR(L"Config file does not contain \"HoloIntervention\" tag. Invalid configuration file.");
          return task_from_result(false);
        }

        auto node = doc->SelectNodes(xpath)->Item(0);

        std::wstring outValue;
        if (!GetAttribute(L"LogLevel", node, outValue) || Log::WStringToLogLevel(outValue) == LogLevelType::LOG_LEVEL_UNKNOWN)
        {
          LOG_WARNING("Log level not found in configuration file. Defaulting to LOG_LEVEL_INFO.");
          Log::instance().SetLogLevel(LogLevelType::LOG_LEVEL_INFO);
        }
        else
        {
          Log::instance().SetLogLevel(Log::WStringToLogLevel(outValue));
        }

        std::shared_ptr<bool> hasError = std::make_shared<bool> (false);
        // Run in order, as some configurations may rely on others
        auto readConfigTask = create_task([this, hasError, doc]()
        {
          *hasError = *hasError || !m_configurables[0]->ReadConfigurationAsync(doc).get();
        });
        for (uint32 i = 1; i < m_configurables.size(); ++i)
        {
          readConfigTask = readConfigTask.then([this, hasError, doc, i]()
          {
            *hasError = *hasError || !m_configurables[i]->ReadConfigurationAsync(doc).get();
          });
        }

        return readConfigTask.then([hasError]()
        {
          return !(*hasError);
        });
      });
    });
  }
}
