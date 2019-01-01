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
#include "Common\Common.h"
#include "Core\ValhallaCore.h"
#include "Debug\Debug.h"
#include "Interfaces\IEngineComponent.h"
#include "Interfaces\ILocatable.h"
#include "Interfaces\ISerializable.h"
#include "Interfaces\IStabilizedComponent.h"
#include "Rendering\\DeviceResources.h"
#include "Rendering\DirectXHelper.h"

// UI includes
#include "UI\Icons.h"

// Physics includes
#include "Physics\PhysicsAPI.h"

// Sound includes
#include "Sound\SoundAPI.h"

// Rendering includes
#include "Rendering\Mesh\MeshRenderer.h"
#include "Rendering\Model\ModelRenderer.h"
#include "Rendering\Notification\NotificationRenderer.h"
#include "Rendering\Slice\SliceRenderer.h"
#include "Rendering\Volume\VolumeRenderer.h"

// Input includes
#include "Input\SpatialInput.h"
#include "Input\VoiceInput.h"

// STL includes
#include <iomanip>
#include <regex>
#include <string>

// Windows includes
#include <windows.graphics.directx.direct3d11.interop.h>

// Intellisense includes
#include "Log\Log.h"
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

// TODO: implement ISystem callback mechanism and core update hooks

namespace Valhalla
{
  //----------------------------------------------------------------------------
  // Loads and initializes application assets when the application is loaded.
  ValhallaCore::ValhallaCore(const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_deviceResources(deviceResources)
  {
    m_deviceResources->RegisterDeviceNotify(this);
  }

  //----------------------------------------------------------------------------
  ValhallaCore::~ValhallaCore()
  {
    m_deviceResources->RegisterDeviceNotify(nullptr);
    UnregisterHolographicEventHandlers();

    if(m_locatabilityIcon != nullptr)
    {
      m_icons->RemoveEntry(m_locatabilityIcon->GetId());
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::SetHolographicSpace(HolographicSpace^ holographicSpace)
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

    m_engineComponents.push_back(m_modelRenderer.get());
    m_engineComponents.push_back(m_sliceRenderer.get());
    m_engineComponents.push_back(m_volumeRenderer.get());
    m_engineComponents.push_back(m_meshRenderer.get());
    m_engineComponents.push_back(m_soundAPI.get());
    m_engineComponents.push_back(m_spatialInput.get());
    m_engineComponents.push_back(m_voiceInput.get());
    m_engineComponents.push_back(m_physicsAPI.get());
    m_engineComponents.push_back(m_icons.get());

    LoadAsync().then([this](bool result)
    {
      if(!result)
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
      catch(Platform::Exception^ e)
      {
        // TODO log callback
        //m_notificationSystem->QueueMessage(L"Unable to initialize audio system. See log.");
        OutputDebugStringW((L"Audio Error" + e->Message)->Data());
      }
    });

    // Use the default SpatialLocator to track the motion of the device.
    m_locator = SpatialLocator::GetDefault();

    for(auto& system : m_locatables)
    {
      system->OnLocatabilityChanged(m_locator->Locatability);
    }

    m_locatabilityChangedToken = m_locator->LocatabilityChanged +=
                                   ref new Windows::Foundation::TypedEventHandler<SpatialLocator^, Object^> (std::bind(&ValhallaCore::OnLocatabilityChanged, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraAddedToken = m_holographicSpace->CameraAdded +=
                           ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraAddedEventArgs^> (std::bind(&ValhallaCore::OnCameraAdded, this, std::placeholders::_1, std::placeholders::_2));

    m_cameraRemovedToken = m_holographicSpace->CameraRemoved +=
                             ref new Windows::Foundation::TypedEventHandler<HolographicSpace^, HolographicSpaceCameraRemovedEventArgs^> (std::bind(&ValhallaCore::OnCameraRemoved, this, std::placeholders::_1, std::placeholders::_2));

    m_attachedReferenceFrame = m_locator->CreateAttachedFrameOfReferenceAtCurrentHeading();

    // Initialize the notification system with a bogus frame to grab sensor data
    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    SpatialCoordinateSystem^ currentCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(holographicFrame->CurrentPrediction->Timestamp);
    SpatialPointerPose^ pose = SpatialPointerPose::TryGetAtTimestamp(currentCoordinateSystem, holographicFrame->CurrentPrediction->Timestamp);

    m_physicsAPI->InitializeSurfaceObserverAsync(currentCoordinateSystem).then([this](task<bool> initTask)
    {
      bool result(false);

      try
      {
        result = initTask.get();
      }
      catch(const std::exception&)
      {
        LOG_ERROR("Unable to initialize surface observers. Mesh data not available.");
        result = false;
      }

      if(!result)
      {
        // TODO log callback
        // TODO : add more robust error handling
        //m_notificationSystem->QueueMessage("Unable to initialize surface observer.");
      }

      LoadAppStateAsync().then([this](bool result)
      {
        if(!result)
        {
          LOG_ERROR("Unable to load app state. Starting new session.");
        }

        LOG(LogLevelType::LOG_LEVEL_INFO, "Engine loading...");
        uint32 componentsReady(0);
        bool engineReady(true);

        // TODO log callback
        //auto messageId = m_notificationSystem->QueueMessage(L"Loading ... 0%");

        double lastValue = 0.0;

        do
        {
          componentsReady = 0;
          engineReady = true;

          std::this_thread::sleep_for(std::chrono::milliseconds(16));

          for(auto& component : m_engineComponents)
          {
#if defined(_DEBUG)

            if(!component->IsReady())
            {
              std::string name(typeid(*component).name());
              std::wstring wname(name.begin(), name.end());
              m_debug->UpdateValue(L"not-ready-comp", wname);
            }

#endif
            engineReady = engineReady && component->IsReady();

            if(component->IsReady())
            {
              componentsReady++;
            }
          }

          if((double)componentsReady / m_engineComponents.size() * 100 == lastValue)
          {
            continue;
          }

          lastValue = (double)componentsReady / m_engineComponents.size() * 100;

          //m_notificationSystem->RemoveMessage(messageId);
          //std::wstringstream wss;
          //wss << L"Loading ... " << std::fixed << std::setprecision(1) << lastValue << L"%";
          //messageId = m_notificationSystem->QueueMessage(wss.str());
        }
        while(!engineReady);

        m_engineReady = true;
      }).then([this]()
      {
        LOG(LogLevelType::LOG_LEVEL_INFO, "Engine loaded.");
        m_voiceInput->EnableVoiceAnalysis(true);
      });
    });
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::UnregisterHolographicEventHandlers()
  {
    if(m_holographicSpace != nullptr)
    {
      if(m_cameraAddedToken.Value != 0)
      {
        m_holographicSpace->CameraAdded -= m_cameraAddedToken;
        m_cameraAddedToken.Value = 0;
      }

      if(m_cameraRemovedToken.Value != 0)
      {
        m_holographicSpace->CameraRemoved -= m_cameraRemovedToken;
        m_cameraRemovedToken.Value = 0;
      }
    }

    if(m_locator != nullptr)
    {
      m_locator->LocatabilityChanged -= m_locatabilityChangedToken;
    }
  }

  //----------------------------------------------------------------------------
  // Updates the application state once per frame.
  HolographicFrame^ ValhallaCore::Update()
  {
    if(!m_engineUserEnabled)
    {
      return nullptr;
    }

    HolographicFrame^ holographicFrame = m_holographicSpace->CreateNextFrame();
    HolographicFramePrediction^ prediction = holographicFrame->CurrentPrediction;

    m_deviceResources->EnsureCameraResources(holographicFrame, prediction);

    SpatialCoordinateSystem^ hmdCoordinateSystem = m_attachedReferenceFrame->GetStationaryCoordinateSystemAtTimestamp(prediction->Timestamp);

    DX::CameraResources* cameraResources(nullptr);

    if(!m_deviceResources->UseHolographicCameraResources<bool>([this, holographicFrame, prediction, hmdCoordinateSystem, &cameraResources](std::map<UINT32, std::unique_ptr<DX::CameraResources>>& cameraResourceMap)
  {
    for(auto cameraPose : prediction->CameraPoses)
      {
        cameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

        if(cameraResources == nullptr)
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

      if(!m_engineReady)
      {
        // Show our welcome screen until the engine is ready!
        m_sliceRenderer->Update(headPose, cameraResources);
      }
      else
      {
        m_voiceInput->Update(m_timer);

        if(headPose != nullptr)
        {
          m_volumeRenderer->Update(cameraResources, hmdCoordinateSystem, headPose);
        }

        m_physicsAPI->Update(hmdCoordinateSystem);

        if(headPose != nullptr)
        {
          m_icons->Update(m_timer, headPose);
          m_soundAPI->Update(m_timer, hmdCoordinateSystem);
          m_sliceRenderer->Update(headPose, cameraResources);
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
  bool ValhallaCore::Render(Windows::Graphics::Holographic::HolographicFrame^ holographicFrame)
  {
    if(m_timer.GetFrameCount() == 0 || !m_engineUserEnabled || holographicFrame == nullptr)
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

      for(auto cameraPose : prediction->CameraPoses)
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

        if(activeCamera)
        {
          if(m_engineReady)
          {
            m_meshRenderer->Render();
            m_modelRenderer->Render();
            m_sliceRenderer->Render();
            m_volumeRenderer->Render();
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
  task<bool> ValhallaCore::SaveAppStateAsync()
  {
    if(m_physicsAPI == nullptr)
    {
      return task_from_result(false);
    }

    return create_task([this]()
    {
      uint32 timer(0);

      while(!m_physicsAPI->IsReady() && timer < 5000)  //(wait for 5s)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        timer += 100;
      }

      if(timer >= 5000)
      {
        return task_from_result(false);
      }

      return m_physicsAPI->SaveAppStateAsync();
    });
  }

  //----------------------------------------------------------------------------
  task<bool> ValhallaCore::LoadAppStateAsync()
  {
    return create_task([this]()
    {
      while(m_physicsAPI == nullptr || !m_physicsAPI->IsReady())
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      return m_physicsAPI->LoadAppStateAsync();
    });
  }

  //----------------------------------------------------------------------------
  uint64 ValhallaCore::GetCurrentFrameNumber() const
  {
    return m_timer.GetFrameCount();
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::OnDeviceLost()
  {
    m_meshRenderer->ReleaseDeviceDependentResources();
    m_physicsAPI->ReleaseDeviceDependentResources();
    m_modelRenderer->ReleaseDeviceDependentResources();
    m_sliceRenderer->ReleaseDeviceDependentResources();
    m_notificationRenderer->ReleaseDeviceDependentResources();
    m_volumeRenderer->ReleaseDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::OnDeviceRestored()
  {
    m_notificationRenderer->CreateDeviceDependentResourcesAsync();
    m_meshRenderer->CreateDeviceDependentResources();
    m_modelRenderer->CreateDeviceDependentResources();
    m_sliceRenderer->CreateDeviceDependentResources();
    m_volumeRenderer->CreateDeviceDependentResourcesAsync();
    m_physicsAPI->CreateDeviceDependentResourcesAsync();
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::RegisterLocatable(ILocatable* locatable)
  {
    if(std::find(begin(m_locatables), end(m_locatables), locatable) == end(m_locatables))
    {
      m_locatables.push_back(locatable);
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::UnregisterLocatable(ILocatable* locatable)
  {
    std::vector<ILocatable*>::iterator it = std::find(begin(m_locatables), end(m_locatables), locatable);

    if(it != end(m_locatables))
    {
      m_locatables.erase(it);
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::RegisterSerializable(ISerializable* component)
  {
    if(std::find(begin(m_serializables), end(m_serializables), component) == end(m_serializables))
    {
      m_serializables.push_back(component);
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::UnregisterSerializable(ISerializable* component)
  {
    std::vector<ISerializable*>::iterator it = std::find(begin(m_serializables), end(m_serializables), component);

    if(it != end(m_serializables))
    {
      m_serializables.erase(it);
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::OnLocatabilityChanged(SpatialLocator^ sender, Object^ args)
  {
    m_locatability = sender->Locatability;

    for(auto& locatable : m_locatables)
    {
      locatable->OnLocatabilityChanged(sender->Locatability);
    }

    if(m_locatabilityIcon == nullptr)
    {
      return;
    }

    switch(sender->Locatability)
    {
      case SpatialLocatability::Unavailable:
        {
          m_locatabilityIcon->GetModel()->SetColour(1.0, 0.0, 0.0);
          // TODO log callback
          //m_notificationSystem->RemoveMessage(m_locatabilityMessage);
          //m_locatabilityMessage = m_notificationSystem->QueueMessage(L"Warning! Positional tracking is unavailable.");
        }
        break;

      case SpatialLocatability::PositionalTrackingActivating:
      case SpatialLocatability::OrientationOnly:
      case SpatialLocatability::PositionalTrackingInhibited:
        // Gaze-locked content still valid
        m_locatabilityIcon->GetModel()->SetColour(1.0, 1.0, 0.0);
        // TODO log callback
        //m_notificationSystem->RemoveMessage(m_locatabilityMessage);
        //m_locatabilityMessage = m_notificationSystem->QueueMessage(L"Re-acquiring positional tracking...");
        break;

      case SpatialLocatability::PositionalTrackingActive:
        m_locatabilityIcon->GetModel()->RenderDefault();
        // TODO log callback
        //m_notificationSystem->RemoveMessage(m_locatabilityMessage);
        //m_locatabilityMessage = m_notificationSystem->QueueMessage(L"Positional tracking is active.", 1.0);
        break;
    }
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::OnCameraAdded(
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
  void ValhallaCore::OnCameraRemoved(
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
  void ValhallaCore::RegisterVoiceCallbacks()
  {
    Input::VoiceInputCallbackMap callbacks;

    m_debug->RegisterVoiceCallbacks(callbacks);
    m_physicsAPI->RegisterVoiceCallbacks(callbacks);
    m_meshRenderer->RegisterVoiceCallbacks(callbacks);

    callbacks[L"end session"] = [this](SpeechRecognitionResult ^ result)
    {
      Log::instance().EndSessionAsync();
      // TODO : log callback
      //m_notificationSystem->QueueMessage(L"Log session ended.");
    };

    callbacks[L"save config"] = [this](SpeechRecognitionResult ^ result)
    {
      m_physicsAPI->SaveAppStateAsync().then([this](bool result)
      {
        if(!result)
        {
          // Physics API didn't save
          // TODO : log callback
          //m_notificationSystem->QueueMessage("Unable to save anchors. Continuing.");
        }

        SaveAsync().then([this](bool result)
        {
          if(result)
          {
            // TODO : log callback
            //m_notificationSystem->QueueMessage(L"Save successful.");
          }
          else
          {
            // TODO : log callback
            //m_notificationSystem->QueueMessage(L"Save failed.");
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
      catch(const std::exception& e)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Failed to compile voice callbacks: ") + e.what());
        // TODO : log callback
        //m_notificationSystem->QueueMessage(L"Unable to initialize voice input system. Critical failure.");
      }
    });
  }

  //----------------------------------------------------------------------------
  void ValhallaCore::SetHolographicFocusPoint(HolographicFramePrediction^ prediction,
      HolographicFrame^ holographicFrame,
      SpatialCoordinateSystem^ currentCoordinateSystem,
      SpatialPointerPose^ pose)
  {
    float maxPriority(PRIORITY_NOT_ACTIVE);
    IStabilizedComponent* winningComponent(nullptr);

    for(auto component : m_engineComponents)
    {
      IStabilizedComponent* stabilizedComponent = dynamic_cast<IStabilizedComponent*>(component);

      if(stabilizedComponent != nullptr && stabilizedComponent->GetStabilizePriority() > maxPriority)
      {
        maxPriority = stabilizedComponent->GetStabilizePriority();
        winningComponent = stabilizedComponent;
      }
    }

    if(winningComponent == nullptr)
    {
      LOG(LogLevelType::LOG_LEVEL_WARNING, "No component returned a stabilization request.");
      return;
    }

    for(auto cameraPose : prediction->CameraPoses)
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
      catch(Platform::Exception^ e)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, e->Message);
      }
    }
  }

  //----------------------------------------------------------------------------
  task<bool> ValhallaCore::SaveAsync()
  {
    // Backup current file
    return create_task(ApplicationData::Current->LocalFolder->TryGetItemAsync(L"configuration.xml")).then([this](IStorageItem ^ item)
    {
      if(item == nullptr)
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
      catch(Platform::Exception^ e)
      {
        WLOG_ERROR(L"Unable to backup existing configuration. Data loss may occur. Error: " + e->Message);
      }

      // Create new document
      auto doc = ref new XmlDocument();

      // Add root node
      auto elem = doc->CreateElement(L"Valhalla");
      doc->AppendChild(elem);

      auto docElem = doc->DocumentElement;

      // Populate document
      std::vector<task<bool>> tasks;

      for(auto comp : m_serializables)
      {
        tasks.push_back(comp->SaveAsync(doc));
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
            catch(const std::exception& e)
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
  task<bool> ValhallaCore::LoadAsync()
  {
    if(m_serializables.size() == 0)
    {
      return task_from_result(true);
    }

    return create_task(ApplicationData::Current->LocalFolder->TryGetItemAsync(L"configuration.xml")).then([this](IStorageItem ^ file)
    {
      if(file == nullptr)
      {
        // If the user specific file doesn't exist, copy the default from the installed location
        return create_task(Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Data\\configuration.xml")).then([this](task<StorageFile^> getTask)
        {
          StorageFile^ file;

          try
          {
            file = getTask.get();
          }
          catch(Platform::Exception^)
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
            catch(Platform::Exception^)
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
      catch(Platform::Exception^)
      {
        // Not local, not installed... what happened!?
        task_from_result(false);
      }

      if(file == nullptr)
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
        catch(Platform::Exception^)
        {
          // Not local, not installed... what happened!?
          return task_from_result(false);
        }

        // Read application level configuration
        Platform::String^ xpath = L"/Valhalla";

        if(doc->SelectNodes(xpath)->Length != 1)
        {
          // No configuration found, use defaults
          LOG_ERROR(L"Config file does not contain \"Valhalla\" tag. Invalid configuration file.");
          return task_from_result(false);
        }

        auto node = doc->SelectNodes(xpath)->Item(0);

        std::wstring outValue;

        if(!GetAttribute(L"LogLevel", node, outValue) || Log::WStringToLogLevel(outValue) == LogLevelType::LOG_LEVEL_UNKNOWN)
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
          *hasError = *hasError || !m_serializables[0]->LoadAsync(doc).get();
        });

        for(uint32 i = 1; i < m_serializables.size(); ++i)
        {
          readConfigTask = readConfigTask.then([this, hasError, doc, i]()
          {
            *hasError = *hasError || !m_serializables[i]->LoadAsync(doc).get();
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
