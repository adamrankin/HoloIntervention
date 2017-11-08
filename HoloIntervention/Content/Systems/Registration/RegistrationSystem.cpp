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

// Local includes
#include "pch.h"
#include "AppView.h"
#include "CameraResources.h"
#include "Common.h"
#include "RegistrationSystem.h"
#include "StepTimer.h"

// Registration types
#include "CameraRegistration.h"
#include "ManualRegistration.h"
#include "OpticalRegistration.h"

// Rendering includes
#include "ModelRenderer.h"
#include "ModelEntry.h"

// Spatial includes
#include "SurfaceMesh.h"

// Physics includes
#include "PhysicsAPI.h"

// System includes
#include "NotificationSystem.h"

// Unnecessary, but removes Intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::REGISTRATION_ANCHOR_NAME = ref new Platform::String(L"Registration");
    const std::wstring RegistrationSystem::REGISTRATION_ANCHOR_MODEL_FILENAME = L"Assets/Models/anchor.cmo";

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod == nullptr)
      {
        return float3(0.f, 0.f, 0.f);
      }
      if (m_currentRegistrationMethod->IsStabilizationActive())
      {
        m_currentRegistrationMethod->GetStabilizedPosition(pose);
      }
      if (m_regAnchor != nullptr)
      {
        const float4x4& pose = m_regAnchorModel->GetCurrentPose();
        return float3(pose.m41, pose.m42, pose.m43);
      }

      // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
      assert(false);
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedVelocity() const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod == nullptr)
      {
        return float3(0.f, 0.f, 0.f);
      }
      if (m_currentRegistrationMethod->IsStabilizationActive())
      {
        m_currentRegistrationMethod->GetStabilizedVelocity();
      }
      if (m_regAnchor != nullptr)
      {
        return m_regAnchorModel->GetVelocity();
      }

      // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
      assert(false);
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float RegistrationSystem::GetStabilizePriority() const
    {
      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod == nullptr)
      {
        return PRIORITY_NOT_ACTIVE;
      }
      if (m_currentRegistrationMethod->IsStabilizationActive())
      {
        return m_currentRegistrationMethod->GetStabilizePriority();
      }
      if (m_regAnchor != nullptr)
      {
        return m_regAnchorModel->IsInFrustum() ? PRIORITY_REGISTRATION : PRIORITY_NOT_ACTIVE;
      }

      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (document->SelectNodes(L"/HoloIntervention")->Length != 1)
        {
          return task_from_result(false);
        }

        auto rootNode = document->SelectNodes(L"/HoloIntervention")->Item(0);

        auto repo = ref new UWPOpenIGTLink::TransformRepository();
        auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD");
        repo->SetTransform(trName, m_correctionMethod->GetRegistrationTransformation() * m_cachedRegistrationTransform, true);
        repo->SetTransformPersistent(trName, true);
        repo->WriteConfiguration(document);

        auto task = m_correctionMethod->WriteConfigurationAsync(document);
        auto result = task.get();
        if (!result)
        {
          return task_from_result(false);
        }

        std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
        for (auto& regMethod : m_knownRegistrationMethods)
        {
          regMethod.second->WriteConfigurationAsync(document);
        }

        return task_from_result(true);
      });
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto repo = ref new UWPOpenIGTLink::TransformRepository();
        auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD");
        if (!repo->ReadConfiguration(document))
        {
          return false;
        }

        IKeyValuePair<bool, float4x4>^ temp;
        temp = repo->GetTransform(trName);
        if (temp->Key)
        {
          m_cachedRegistrationTransform = transpose(temp->Value);
        }

        // Test each known registration method ReadConfigurationAsync and store if known
        for (auto pair :
             {
               std::pair<std::wstring, std::shared_ptr<Algorithm::IRegistrationMethod>>(L"Optical", std::make_shared<Algorithm::OpticalRegistration>(m_notificationSystem, m_networkSystem)),
               std::pair<std::wstring, std::shared_ptr<Algorithm::IRegistrationMethod>>(L"Camera", std::make_shared<Algorithm::CameraRegistration>(m_notificationSystem, m_networkSystem, m_modelRenderer)),
               std::pair<std::wstring, std::shared_ptr<Algorithm::IRegistrationMethod>>(L"Manual", std::make_shared<Algorithm::ManualRegistration>(m_networkSystem))
             })
        {
          bool result = pair.second->ReadConfigurationAsync(document).get();
          if (result)
          {
            m_knownRegistrationMethods[pair.first] = pair.second;
          }
        }

        auto task = m_correctionMethod->ReadConfigurationAsync(document);
        auto result = task.get();
        if (!result)
        {
          return false;
        }
        m_configDocument = document;
        m_componentReady = true;

        return true;
      });
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(NetworkSystem& networkSystem, Physics::PhysicsAPI& physicsAPI, NotificationSystem& notificationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_modelRenderer(modelRenderer)
      , m_physicsAPI(physicsAPI)
      , m_currentRegistrationMethod(nullptr)
      , m_correctionMethod(std::make_shared<Algorithm::ManualRegistration>(networkSystem))
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
    void RegistrationSystem::Update(DX::StepTimer& timer, SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose)
    {
      // Anchor placement logic
      if (m_regAnchorRequested)
      {
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
        m_regAnchorRequested = false;
      }

      Platform::IBox<float4x4>^ transformContainer(nullptr);
      // Anchor model position update logic
      if (m_regAnchor != nullptr)
      {
        transformContainer = m_regAnchor->CoordinateSystem->TryGetTransformTo(coordinateSystem);
        if (transformContainer != nullptr)
        {
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
      }

      std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
      if (m_currentRegistrationMethod != nullptr && m_currentRegistrationMethod->IsStarted() && transformContainer != nullptr)
      {
        m_currentRegistrationMethod->Update(headPose, coordinateSystem, transformContainer);
      }

      if (m_correctionMethod->IsStarted())
      {
        m_correctionMethod->Update(headPose, coordinateSystem, transformContainer);
      }
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task([ = ]()
      {
        if (m_physicsAPI.HasAnchor(REGISTRATION_ANCHOR_NAME))
        {
          m_forcePose = true;
          m_regAnchor = m_physicsAPI.GetAnchor(REGISTRATION_ANCHOR_NAME);
          m_regAnchorModel->SetVisible(true);
          if (m_currentRegistrationMethod != nullptr)
          {
            m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
          }
        }
      });
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::IsCameraActive() const
    {
      auto camReg = dynamic_cast<Algorithm::CameraRegistration*>(m_currentRegistrationMethod.get());
      return camReg != nullptr && camReg->IsCameraActive();
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
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
        if (dynamic_cast<Algorithm::CameraRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(L"Camera") == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No camera configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[L"Camera"];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->RegisterTransformUpdatedCallback(std::bind(&RegistrationSystem::OnRegistrationComplete, this, std::placeholders::_1));
        m_currentRegistrationMethod->ReadConfigurationAsync(m_configDocument).then([this](bool result)
        {
          std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
          if (!result)
          {
            return task_from_result(false);
          }
          return m_currentRegistrationMethod->StartAsync();
        }).then([this](bool result)
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
        if (dynamic_cast<Algorithm::OpticalRegistration*>(m_currentRegistrationMethod.get()) != nullptr && m_currentRegistrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        if (m_knownRegistrationMethods.find(L"Optical") == m_knownRegistrationMethods.end())
        {
          m_notificationSystem.QueueMessage(L"No optical configuration defined. Please add the necessary information to the configuration file and try again.");
          return;
        }
        m_currentRegistrationMethod = m_knownRegistrationMethods[L"Optical"];
        m_currentRegistrationMethod->SetWorldAnchor(m_regAnchor);
        m_currentRegistrationMethod->RegisterTransformUpdatedCallback(std::bind(&RegistrationSystem::OnRegistrationComplete, this, std::placeholders::_1));
        m_currentRegistrationMethod->ReadConfigurationAsync(m_configDocument).then([this](bool result)
        {
          std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
          if (!result)
          {
            return task_from_result(false);
          }
          return m_currentRegistrationMethod->StartAsync();
        }).then([this](bool result)
        {
          if (!result)
          {
            std::lock_guard<std::mutex> guard(m_registrationMethodMutex);
            m_currentRegistrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start optical registration.");
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

      callbackMap[L"correct translation"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_correctionMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Correction already running.");
          return;
        }

        m_correctionMethod->RegisterTransformUpdatedCallback(std::bind(&RegistrationSystem::OnCorrectionComplete, this, std::placeholders::_1));
        m_correctionMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            m_notificationSystem.QueueMessage(L"Unable to start correction.");
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Correction started...");
          }
        });
      };

      callbackMap[L"stop correction"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!m_correctionMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Already stopped.");
          return;
        }
        m_correctionMethod->StopAsync().then([this](bool result)
        {
          if (result)
          {
            m_notificationSystem.QueueMessage(L"Correction stopped.");
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Error when stopping correction.");
          }
        });
      };

      callbackMap[L"reset correction"] = [this](SpeechRecognitionResult ^ result)
      {
        m_correctionMethod->ResetRegistration();
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
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::GetReferenceToCoordinateSystemTransformation(SpatialCoordinateSystem^ requestedCoordinateSystem, float4x4& outTransform)
    {
      if (m_cachedRegistrationTransform == float4x4::identity())
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

        float4x4 correctedRegistration = m_cachedRegistrationTransform;
        correctedRegistration.m41 += m_cachedCorrectionTransform.m41;
        correctedRegistration.m42 += m_cachedCorrectionTransform.m42;
        correctedRegistration.m43 += m_cachedCorrectionTransform.m43;
        outTransform = correctedRegistration * anchorToRequestedBox->Value;
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

        m_cachedRegistrationTransform = unscaledMatrix;
      }
      else
      {
        m_cachedRegistrationTransform = registrationTransformation;
      }
    }

    //-----------------------------------------------------------------------------
    void RegistrationSystem::OnCorrectionComplete(float4x4 correctionTransformation)
    {
      m_cachedCorrectionTransform = correctionTransformation;

      if (m_cachedRegistrationTransform != float4x4::identity())
      {
        // Apply rotation to correction (correction is relative translation only for now)
        float4x4 regToAnchorRot = m_cachedRegistrationTransform;
        regToAnchorRot.m41 = 0.f;
        regToAnchorRot.m42 = 0.f;
        regToAnchorRot.m43 = 0.f;

        float3 pose_ref(correctionTransformation.m41, correctionTransformation.m42, correctionTransformation.m43);
        float3 pose_anch = transform(pose_ref, regToAnchorRot);
        m_cachedCorrectionTransform = make_float4x4_translation(pose_anch);
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