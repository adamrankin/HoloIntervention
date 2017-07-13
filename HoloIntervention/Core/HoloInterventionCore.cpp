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
#include "IConfigurable.h"
#include "IEngineComponent.h"
#include "IStabilizedComponent.h"

// System includes
#include "GazeSystem.h"
#include "IconSystem.h"
#include "ImagingSystem.h"
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "SplashSystem.h"
#include "ToolSystem.h"

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
    m_modelRenderer = std::make_unique<Rendering::ModelRenderer>(m_deviceResources, m_timer);
    m_sliceRenderer = std::make_unique<Rendering::SliceRenderer>(m_deviceResources, m_timer);
    m_volumeRenderer = std::make_unique<Rendering::VolumeRenderer>(m_deviceResources, m_timer);
    m_meshRenderer = std::make_unique<Rendering::MeshRenderer>(*m_notificationSystem.get(), m_deviceResources);

    m_soundAPI = std::make_unique<Sound::SoundAPI>();

    m_spatialInput = std::make_unique<Input::SpatialInput>();
    m_voiceInput = std::make_unique<Input::VoiceInput>(*m_notificationSystem.get(), *m_soundAPI.get());

    m_networkSystem = std::make_unique<System::NetworkSystem>(*m_notificationSystem.get(), *m_voiceInput.get());
    m_physicsAPI = std::make_unique<Physics::PhysicsAPI>(*m_notificationSystem.get(), m_deviceResources, m_timer);

    m_registrationSystem = std::make_unique<System::RegistrationSystem>(*m_networkSystem.get(), *m_physicsAPI.get(), *m_notificationSystem.get(), *m_modelRenderer.get());
    m_toolSystem = std::make_unique<System::ToolSystem>(*m_notificationSystem.get(), *m_registrationSystem.get(), *m_modelRenderer.get(), *m_networkSystem.get());
    m_iconSystem = std::make_unique<System::IconSystem>(*m_notificationSystem.get(), *m_registrationSystem.get(), *m_networkSystem.get(), *m_toolSystem.get(), *m_voiceInput.get(), *m_modelRenderer.get());
    m_gazeSystem = std::make_unique<System::GazeSystem>(*m_notificationSystem.get(), *m_physicsAPI.get(), *m_modelRenderer.get());
    m_imagingSystem = std::make_unique<System::ImagingSystem>(*m_registrationSystem.get(), *m_notificationSystem.get(), *m_sliceRenderer.get(), *m_volumeRenderer.get(), *m_networkSystem.get());
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
    m_engineComponents.push_back(m_networkSystem.get());
    m_engineComponents.push_back(m_gazeSystem.get());
    m_engineComponents.push_back(m_toolSystem.get());
    m_engineComponents.push_back(m_registrationSystem.get());
    m_engineComponents.push_back(m_imagingSystem.get());
    m_engineComponents.push_back(m_iconSystem.get());
    m_engineComponents.push_back(m_splashSystem.get());

    m_configurableComponents.push_back(m_toolSystem.get());
    m_configurableComponents.push_back(m_registrationSystem.get());
    m_configurableComponents.push_back(m_networkSystem.get());
    m_configurableComponents.push_back(m_imagingSystem.get());
    m_configurableComponents.push_back(m_iconSystem.get());

    ReadConfigurationAsync();

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

    LoadAppStateAsync();

    LOG(LogLevelType::LOG_LEVEL_INFO, "Engine started.");
    m_engineBuilt = true;
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
    if (!m_engineBuilt)
    {
      return nullptr;
    }

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
        if (cameraResources == nullptr)
        {
          return false;
        }
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

      if (m_engineReady)
      {
        m_splashSystem->EndSplash();
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
        // Show our welcome screen until the engine is ready!
        m_splashSystem->Update(m_timer, hmdCoordinateSystem, headPose);
        m_sliceRenderer->Update(headPose, cameraResources);
      }
      else
      {
        if (headPose != nullptr)
        {
          m_volumeRenderer->Update(cameraResources, hmdCoordinateSystem, headPose);
        }
        m_imagingSystem->Update(m_timer, hmdCoordinateSystem);
        m_toolSystem->Update(m_timer, hmdCoordinateSystem);

        m_physicsAPI->Update(hmdCoordinateSystem);

        if (headPose != nullptr)
        {
          m_registrationSystem->Update(m_timer, hmdCoordinateSystem, headPose);
          m_gazeSystem->Update(m_timer, hmdCoordinateSystem, headPose);
          m_iconSystem->Update(m_timer, headPose);
          m_soundAPI->Update(m_timer, hmdCoordinateSystem);
          m_sliceRenderer->Update(headPose, cameraResources);
          m_notificationSystem->Update(headPose, m_timer);
        }

        m_meshRenderer->Update(m_timer, hmdCoordinateSystem);
        m_modelRenderer->Update(cameraResources);
      }
    });

    SpatialPointerPose^ headPose = SpatialPointerPose::TryGetAtTimestamp(hmdCoordinateSystem, prediction->Timestamp);
    SetHolographicFocusPoint(prediction, holographicFrame, hmdCoordinateSystem, headPose);

    return holographicFrame;
  }

  //----------------------------------------------------------------------------
  bool HoloInterventionCore::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame)
  {
    if (!m_engineBuilt || m_timer.GetFrameCount() == 0)
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
    if (!m_engineBuilt)
    {
      return create_task([]() {});
    }
    return m_physicsAPI->SaveAppStateAsync();
  }

  //----------------------------------------------------------------------------
  task<void> HoloInterventionCore::LoadAppStateAsync()
  {
    if (!m_engineBuilt)
    {
      // This can happen when the app is starting
      return create_task([]() {});
    }

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
    m_networkSystem->RegisterVoiceCallbacks(callbacks);
    m_physicsAPI->RegisterVoiceCallbacks(callbacks);
    m_toolSystem->RegisterVoiceCallbacks(callbacks);
    m_imagingSystem->RegisterVoiceCallbacks(callbacks);
    m_meshRenderer->RegisterVoiceCallbacks(callbacks);
    m_registrationSystem->RegisterVoiceCallbacks(callbacks);

    callbacks[L"end session"] = [this](SpeechRecognitionResult ^ result)
    {
      Log::instance().EndSessionAsync();
      m_notificationSystem->QueueMessage(L"Log session ended.");
    };

    callbacks[L"save config"] = [this](SpeechRecognitionResult ^ result)
    {
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
        LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Failed to compile callbacks: ") + e.what());
      }
    });
  }

  //----------------------------------------------------------------------------
  void HoloInterventionCore::SetHolographicFocusPoint(HolographicFramePrediction^ prediction,
      HolographicFrame^ holographicFrame,
      SpatialCoordinateSystem^ currentCoordinateSystem,
      SpatialPointerPose^ pose)
  {
    float maxPriority(-1.f);
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
      float3 focusPointNormal = pose->Head->UpDirection;
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
          LOG(LogLevelType::LOG_LEVEL_ERROR, e->Message);
        }
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
        return task_from_result((StorageFile^)nullptr);
      }
      else
      {
        StorageFile^ file = dynamic_cast<StorageFile^>(item);
        return create_task(file->CopyAsync(ApplicationData::Current->LocalFolder, L"configuration.xml", Windows::Storage::NameCollisionOption::GenerateUniqueName));
      }
    }).then([this](task<StorageFile^> copyTask)
    {
      copyTask.wait();

      // Create new document
      auto doc = ref new XmlDocument();

      // Add root node
      auto elem = doc->CreateElement(L"HoloIntervention");
      doc->AppendChild(elem);

      auto docElem = doc->DocumentElement;

      // Populate document
      std::vector<task<bool>> tasks;
      for (auto comp : m_configurableComponents)
      {
        tasks.push_back(comp->WriteConfigurationAsync(doc));
      }

      return when_all(begin(tasks), end(tasks)).then([this, doc](std::vector<bool> results)
      {
        // Write document to disk
        return create_task(ApplicationData::Current->LocalFolder->CreateFileAsync(L"configuration.xml", Windows::Storage::CreationCollisionOption::ReplaceExisting)).then([this, doc](StorageFile ^ file)
        {
          return create_task(doc->SaveToFileAsync(file)).then([this](task<void> saveAction)
          {
            try
            {
              saveAction.wait();
            }
            catch (Platform::Exception^ e)
            {
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
    if (m_configurableComponents.size() == 0)
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
              return (StorageFile^)nullptr;
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

        std::shared_ptr<bool> hasError = std::make_shared<bool>(false);
        // Run in order, as some configurations may rely on others
        auto readConfigTask = create_task([this, hasError, doc]()
        {
          *hasError = *hasError || !m_configurableComponents[0]->ReadConfigurationAsync(doc).get();
        });
        for (uint32 i = 1; i < m_configurableComponents.size(); ++i)
        {
          readConfigTask = readConfigTask.then([this, hasError, doc, i]()
          {
            *hasError = *hasError || !m_configurableComponents[i]->ReadConfigurationAsync(doc).get();
          });
        }

        return readConfigTask.then([hasError]()
        {
          return *hasError;
        });
      });
    });
  }

}