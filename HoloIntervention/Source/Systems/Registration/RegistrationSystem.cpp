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
#include "CameraResources.h"
#include "Common.h"
#include "RegistrationSystem.h"
#include "StepTimer.h"

// Registration types
#include "CameraRegistration.h"
#include "ModelAlignmentRegistration.h"
#include "OpticalRegistration.h"
#include "ToolBasedRegistration.h"

// Rendering includes
#include "ModelRenderer.h"
#include "Model.h"

// Spatial includes
#include "SurfaceMesh.h"

// Input includes
#include "SpatialInput.h"

// Physics includes
#include "PhysicsAPI.h"

// UI includes
#include "Icons.h"

// System includes
#include "NotificationSystem.h"

// Unnecessary, but removes Intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::REGISTRATION_ANCHOR_NAME = ref new Platform::String(L"Registration");
    const std::wstring RegistrationSystem::REGISTRATION_ANCHOR_MODEL_FILENAME = L"anchor";

    std::array<std::wstring, REGISTRATIONTYPE_COUNT> RegistrationSystem::REGISTRATION_TYPE_NAMES =
    {
      L"None",
      L"ToolBased",
      L"Optical",
      L"Camera",
      L"ModelAlignment"
    };

    //----------------------------------------------------------------------------
    void RegistrationSystem::OnLocatabilityChanged(Windows::Perception::Spatial::SpatialLocatability locatability)
    {
      m_messageSent = false;
      m_locatability = locatability;
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod != nullptr && m_currentRegistrationMethod->IsStabilizationActive())
      {
        return m_currentRegistrationMethod->GetStabilizedPosition(pose);
      }
      else
      {
        const float4x4& pose = m_regAnchorModel->GetCurrentPose();
        return float3(pose.m41, pose.m42, pose.m43);
      }
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedVelocity() const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod != nullptr && m_currentRegistrationMethod->IsStabilizationActive())
      {
        return m_currentRegistrationMethod->GetStabilizedVelocity();
      }
      else
      {
        return m_regAnchorModel->GetVelocity();
      }
    }

    //----------------------------------------------------------------------------
    float RegistrationSystem::GetStabilizePriority() const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod != nullptr && m_currentRegistrationMethod->IsStabilizationActive())
      {
        return m_currentRegistrationMethod->GetStabilizePriority();
      }
      else if (m_regAnchorModel != nullptr && m_regAnchorModel->IsInFrustum())
      {
        return PRIORITY_REGISTRATION;
      }
      else
      {
        return PRIORITY_NOT_ACTIVE;
      }
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (document->SelectNodes(L"/HoloIntervention")->Length != 1)
        {
          return false;
        }

        auto rootNode = document->SelectNodes(L"/HoloIntervention")->Item(0);

        if (m_cachedReferenceToAnchor != float4x4::identity())
        {
          auto repo = ref new UWPOpenIGTLink::TransformRepository();
          auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"Anchor");
          if (!repo->SetTransform(trName, m_cachedReferenceToAnchor, true))
          {
            return false;
          }
          if (!repo->SetTransformPersistent(trName, true))
          {
            return false;
          }

          bool result = repo->WriteConfiguration(document);
          if (!result)
          {
            LOG_ERROR("Unable to write repository configuration in RegistrationSystem::WriteConfigurationAsync");
            return false;
          }
        }

        bool result = true;
        for (auto& pair : m_knownRegistrationMethods)
        {
          result &= pair.second->WriteConfigurationAsync(document).get();
        }
        return result;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto repo = ref new UWPOpenIGTLink::TransformRepository();
        auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"Anchor");
        if (!repo->ReadConfiguration(document))
        {
          return false;
        }

        IKeyValuePair<bool, float4x4>^ temp;
        temp = repo->GetTransform(trName);
        if (temp->Key)
        {
          m_cachedReferenceToAnchor = transpose(temp->Value);
        }

        // Test each known registration method ReadConfigurationAsync and store if known
        for (auto pair :
             {
               std::pair<std::wstring, std::shared_ptr<IRegistrationMethod>>(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_OPTICAL], std::make_shared<OpticalRegistration>(IConfigurable::m_core, m_notificationSystem, m_networkSystem)),
               std::pair<std::wstring, std::shared_ptr<IRegistrationMethod>>(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_MODELALIGNMENT], std::make_shared<ModelAlignmentRegistration>(IConfigurable::m_core, m_notificationSystem, m_networkSystem, m_modelRenderer, m_spatialInput, m_icons, m_debug, m_timer)),
               std::pair<std::wstring, std::shared_ptr<IRegistrationMethod>>(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_CAMERA], std::make_shared<CameraRegistration>(IConfigurable::m_core, m_notificationSystem, m_networkSystem, m_modelRenderer)),
               std::pair<std::wstring, std::shared_ptr<IRegistrationMethod>>(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_TOOLBASED], std::make_shared<ToolBasedRegistration>(IConfigurable::m_core, m_networkSystem))
             })
        {
          bool result = pair.second->ReadConfigurationAsync(document).get();
          if (result)
          {
            m_knownRegistrationMethods[pair.first] = pair.second;
          }
          pair.second->RegisterTransformUpdatedCallback(std::bind(&RegistrationSystem::OnRegistrationComplete, this, std::placeholders::_1));
        }

        m_configDocument = document;
        m_componentReady = true;

        return true;
      });
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(HoloInterventionCore& core, NetworkSystem& networkSystem, Physics::PhysicsAPI& physicsAPI, NotificationSystem& notificationSystem, Rendering::ModelRenderer& modelRenderer, Input::SpatialInput& spatialInput, UI::Icons& icons, Debug& debug, DX::StepTimer& timer)
      : ILocatable(core)
      , IConfigurable(core)
      , m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_modelRenderer(modelRenderer)
      , m_physicsAPI(physicsAPI)
      , m_icons(icons)
      , m_debug(debug)
      , m_spatialInput(spatialInput)
      , m_currentRegistrationMethod(nullptr)
      , m_timer(timer)
    {
      m_modelRenderer.AddModelAsync(REGISTRATION_ANCHOR_MODEL_FILENAME).then([this](uint64 m_regAnchorModelId)
      {
        if (m_regAnchorModelId != INVALID_TOKEN)
        {
          m_regAnchorModel = m_modelRenderer.GetModel(m_regAnchorModelId);
        }
        if (m_regAnchorModel == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Unable to retrieve anchor model.");
          return;
        }
        m_regAnchorModel->SetVisible(false);
        m_regAnchorModel->EnablePoseLerp(true);
        m_regAnchorModel->SetPoseLerpRate(4.f);
      });
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::~RegistrationSystem()
    {
      m_regAnchorModel = nullptr;
      m_regAnchorModelId = 0;
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::Update(SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose, HolographicCameraPose^ cameraPose)
    {
      // Anchor placement logic
      if (m_regAnchorRequested)
      {
        if (m_locatability != SpatialLocatability::PositionalTrackingActive && !m_messageSent)
        {
          m_notificationSystem.QueueMessage(L"Positional tracking required for dropping an anchor.");
          m_messageSent = true;
          return;
        }

        if (m_physicsAPI.DropAnchorAtIntersectionHit(REGISTRATION_ANCHOR_NAME, coordinateSystem, headPose))
        {
          if (m_regAnchorModel != nullptr)
          {
            m_regAnchorModel->SetVisible(true);
            m_regAnchor = m_physicsAPI.GetAnchor(REGISTRATION_ANCHOR_NAME);
          }

          m_notificationSystem.QueueMessage(L"Anchor created.");

          m_physicsAPI.SaveAppStateAsync();
        }
        else
        {
          m_notificationSystem.QueueMessage(L"Unable to drop anchor.");
        }
        m_regAnchorRequested = false;
      }

      Platform::IBox<float4x4>^ transformContainer(nullptr);
      // Anchor model position update logic
      if (m_regAnchor != nullptr)
      {
        transformContainer = m_regAnchor->CoordinateSystem->TryGetTransformTo(coordinateSystem);
        if (transformContainer != nullptr)
        {
          m_regAnchorModel->SetVisible(true);
          if (m_forcePose)
          {
            m_regAnchorModel->SetCurrentPose(transformContainer->Value);
            m_forcePose = false;
          }
          else
          {
            m_regAnchorModel->SetDesiredPose(transformContainer->Value);
          }
        }
        else if (m_locatability != SpatialLocatability::PositionalTrackingActive)
        {
          // World locked content not available, head-locked only
          m_regAnchorModel->SetVisible(false);
        }
      }

      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod != nullptr && m_currentRegistrationMethod->IsStarted() && transformContainer != nullptr)
      {
        m_currentRegistrationMethod->Update(headPose, coordinateSystem, transformContainer, cameraPose);
      }
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task([this]()
      {
        if (m_physicsAPI.HasAnchor(REGISTRATION_ANCHOR_NAME))
        {
          m_forcePose = true;
          m_regAnchor = m_physicsAPI.GetAnchor(REGISTRATION_ANCHOR_NAME);
          if (m_regAnchor != nullptr)
          {
            {
              std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
              if (m_currentRegistrationMethod != nullptr)
              {
                m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
              }
            }
            while (m_regAnchorModel == nullptr)
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            m_regAnchorModel->SetVisible(true);
          }
          else
          {
            LOG_ERROR("Anchor exists by name but is nullptr.");
            return false;
          }
        }

        return true;
      });
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::IsCameraActive() const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      auto camReg = dynamic_cast<CameraRegistration*>(m_currentRegistrationMethod.get());
      return camReg != nullptr && camReg->IsCameraActive();
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      for (auto& method : m_knownRegistrationMethods)
      {
        method.second->RegisterVoiceCallbacks(callbackMap);
      }

      callbackMap[L"debug registration"] = [this](SpeechRecognitionResult ^ result)
      {
        m_cachedReferenceToAnchor = float4x4::identity();
        m_cachedReferenceToAnchor.m41 = 0.01f;
      };

      callbackMap[L"drop anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        m_regAnchorRequested = true;
      };

      callbackMap[L"remove anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        m_currentRegistrationMethod->StopAsync().then([this](bool result)
        {
          if (m_regAnchorModel)
          {
            m_regAnchorModel->SetVisible(false);
          }
          if (m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME) == 1)
          {
            m_notificationSystem.QueueMessage(L"Anchor \"" + REGISTRATION_ANCHOR_NAME + "\" removed.");
          }
        });
      };

      callbackMap[L"start camera registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (dynamic_cast<CameraRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_CAMERA]) == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No camera configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_CAMERA]];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
            m_currentRegistrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start camera registration.");
          }
        });
      };

      callbackMap[L"start optical registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (dynamic_cast<OpticalRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_OPTICAL]) == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No optical configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_OPTICAL]];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
            m_currentRegistrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start optical registration.");
          }
        });
      };

      callbackMap[L"start alignment registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (dynamic_cast<ModelAlignmentRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_MODELALIGNMENT]) == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No alignment configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_MODELALIGNMENT]];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
            m_currentRegistrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start alignment registration.");
          }
        });
      };

      callbackMap[L"start tool registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (dynamic_cast<ToolBasedRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_TOOLBASED]) == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No alignment configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[REGISTRATION_TYPE_NAMES[REGISTRATIONTYPE_TOOLBASED]];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
            m_currentRegistrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start tool based registration.");
          }
        });
      };

      callbackMap[L"stop registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_currentRegistrationMethod == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Registration not running.");
          return;
        }
        m_currentRegistrationMethod->StopAsync().then([this](bool result)
        {
          std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
          m_currentRegistrationMethod = nullptr;
        });
      };

      callbackMap[L"reset registration"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_currentRegistrationMethod != nullptr)
        {
          m_currentRegistrationMethod->ResetRegistration();
        }
      };

      callbackMap[L"enable registration viz"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_currentRegistrationMethod != nullptr)
        {
          m_currentRegistrationMethod->EnableVisualization(true);
          m_notificationSystem.QueueMessage(L"Visualization enabled.");
        }
      };

      callbackMap[L"disable registration viz"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_currentRegistrationMethod != nullptr)
        {
          m_currentRegistrationMethod->EnableVisualization(false);
          m_notificationSystem.QueueMessage(L"Visualization disabled.");
        }
      };

      callbackMap[L"anchor up"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_regAnchor == nullptr)
        {
          return;
        }

        auto anchor = m_regAnchor->TryCreateRelativeTo(m_regAnchor->CoordinateSystem, float3(0.f, 0.005f, 0.f)); // 5 mm in Y
        if (anchor != nullptr)
        {
          m_regAnchor = nullptr;
          m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME);
          m_physicsAPI.AddOrUpdateAnchor(anchor, REGISTRATION_ANCHOR_NAME);
          m_regAnchor = anchor;
          m_physicsAPI.SaveAppStateAsync();
        }
      };

      callbackMap[L"anchor big up"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_regAnchor == nullptr)
        {
          return;
        }

        auto anchor = m_regAnchor->TryCreateRelativeTo(m_regAnchor->CoordinateSystem, float3(0.f, 0.01f, 0.f)); // 1 cm in Y
        if (anchor != nullptr)
        {
          m_regAnchor = nullptr;
          m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME);
          m_physicsAPI.AddOrUpdateAnchor(anchor, REGISTRATION_ANCHOR_NAME);
          m_regAnchor = anchor;
          m_physicsAPI.SaveAppStateAsync();
        }
      };

      callbackMap[L"anchor down"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_regAnchor == nullptr)
        {
          return;
        }

        auto anchor = m_regAnchor->TryCreateRelativeTo(m_regAnchor->CoordinateSystem, float3(0.f, -0.005f, 0.f)); // 5 mm in -Y
        if (anchor != nullptr)
        {
          m_regAnchor = nullptr;
          m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME);
          m_physicsAPI.AddOrUpdateAnchor(anchor, REGISTRATION_ANCHOR_NAME);
          m_regAnchor = anchor;
          m_physicsAPI.SaveAppStateAsync();
        }
      };

      callbackMap[L"anchor big down"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        if (m_regAnchor == nullptr)
        {
          return;
        }

        auto anchor = m_regAnchor->TryCreateRelativeTo(m_regAnchor->CoordinateSystem, float3(0.f, -0.01f, 0.f)); // 1 cm in -Y
        if (anchor != nullptr)
        {
          m_regAnchor = nullptr;
          m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME);
          m_physicsAPI.AddOrUpdateAnchor(anchor, REGISTRATION_ANCHOR_NAME);
          m_regAnchor = anchor;
          m_physicsAPI.SaveAppStateAsync();
        }
      };
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::GetReferenceToCoordinateSystemTransformation(SpatialCoordinateSystem^ requestedCoordinateSystem, float4x4& outTransform)
    {
      if (m_cachedReferenceToAnchor == float4x4::identity())
      {
        return false;
      }

      if (m_regAnchor == nullptr)
      {
        return false;
      }

      try
      {
        Platform::IBox<float4x4>^ anchorToRequestedBox = m_regAnchor->CoordinateSystem->TryGetTransformTo(requestedCoordinateSystem);
        if (anchorToRequestedBox == nullptr)
        {
          return false;
        }

        outTransform = m_cachedReferenceToAnchor * anchorToRequestedBox->Value;
        return true;
      }
      catch (Platform::Exception^ e)
      {
        return false;
      }

      return false;
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::OnRegistrationComplete(float4x4 registrationTransformation)
    {
      if (!CheckRegistrationValidity(registrationTransformation))
      {
        // Remove any scaling, for now, assume 1:1 (mm to mm)
        float3 scaling;
        quaternion rotation;
        float3 translation;
        decompose(registrationTransformation, &scaling, &rotation, &translation);
        auto unscaledMatrix = make_float4x4_from_quaternion(rotation);
        unscaledMatrix.m41 = translation.x;
        unscaledMatrix.m42 = translation.y;
        unscaledMatrix.m43 = translation.z;

        LOG(LogLevelType::LOG_LEVEL_INFO, L"Registration matrix scaling: " + scaling.x.ToString() + L", " + scaling.y.ToString() + L", " + scaling.z.ToString());

        m_cachedReferenceToAnchor = unscaledMatrix;
      }
      else
      {
        m_cachedReferenceToAnchor = registrationTransformation;
      }
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::CheckRegistrationValidity(float4x4 registrationTransformation)
    {
      // Check orthogonality of basis vectors
      float3 xAxis = normalize(float3(registrationTransformation.m11, registrationTransformation.m21, registrationTransformation.m31));
      float3 yAxis = normalize(float3(registrationTransformation.m12, registrationTransformation.m22, registrationTransformation.m32));
      float3 zAxis = normalize(float3(registrationTransformation.m13, registrationTransformation.m23, registrationTransformation.m33));

      if (!IsFloatEqual(dot(xAxis, yAxis), 0.f) ||
          !IsFloatEqual(dot(xAxis, zAxis), 0.f) ||
          !IsFloatEqual(dot(yAxis, zAxis), 0.f))
      {
        // Not orthogonal!
        return false;
      }

      // Check to see if scale is 1
      // TODO : this is currently hardcoded as tracker is expected to produce units in mm, eventually this assumption will be removed
      // the scale is currently not 1, this is a bug
      float3 scale;
      quaternion rot;
      float3 translation;
      if (!decompose(registrationTransformation, &scale, &rot, &translation))
      {
        return false;
      }

      LOG(LogLevelType::LOG_LEVEL_DEBUG, "scale: " + scale.x.ToString() + L" " + scale.y.ToString() + L" " + scale.z.ToString());

      if (!IsFloatEqual(scale.x, 1.f) || !IsFloatEqual(scale.y, 1.f) || !IsFloatEqual(scale.z, 1.f))
      {
        return false;
      }

      return true;
    }
  }
}