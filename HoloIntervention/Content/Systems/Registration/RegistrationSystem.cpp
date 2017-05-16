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
#include "RegistrationSystem.h"
#include "CameraResources.h"

// Registration types
#include "CameraRegistration.h"
#include "OpticalRegistration.h"

// Common includes
#include "Common.h"
#include "StepTimer.h"

// Rendering includes
#include "ModelRenderer.h"
#include "ModelEntry.h"

// Spatial includes
#include "SurfaceMesh.h"

// Physics includes
#include "PhysicsAPI.h"

// System includes
#include "NotificationSystem.h"

// Unnecessary, but removes intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
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
      if (m_registrationMethod->IsStabilizationActive())
      {
        m_registrationMethod->GetStabilizedPosition(pose);
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
    float3 RegistrationSystem::GetStabilizedNormal(SpatialPointerPose^ pose) const
    {
      if (m_registrationMethod->IsStabilizationActive())
      {
        m_registrationMethod->GetStabilizedNormal(pose);
      }
      if (m_regAnchor != nullptr)
      {
        return ExtractNormal(m_regAnchorModel->GetCurrentPose());
      }

      // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
      assert(false);
      return float3(0.f, 1.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedVelocity() const
    {
      if (m_registrationMethod->IsStabilizationActive())
      {
        m_registrationMethod->GetStabilizedVelocity();
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
      if (m_registrationMethod->IsStabilizationActive())
      {
        return m_registrationMethod->GetStabilizePriority();
      }
      if (m_regAnchor != nullptr)
      {
        // TODO : stabilization values?
        return m_regAnchorModel->IsInFrustum() ? 1.f : PRIORITY_NOT_ACTIVE;
      }

      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> RegistrationSystem::WriteConfigurationAsync(XmlDocument^ document)
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
        repo->SetTransform(trName, m_cachedRegistrationTransform, true);
        repo->SetTransformPersistent(trName, true);
        repo->WriteConfiguration(document);

        return m_registrationMethod->WriteConfigurationAsync(document);
      });
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> RegistrationSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto repo = ref new UWPOpenIGTLink::TransformRepository();
        auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD");
        if (!repo->ReadConfiguration(document))
        {
          return task_from_result(false);
        }

        float4x4 temp;
        if (repo->GetTransform(trName, &temp))
        {
          m_cachedRegistrationTransform = transpose(temp);
        }
        m_componentReady = true;

        return m_registrationMethod->ReadConfigurationAsync(document);
      });
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(NetworkSystem& networkSystem, Physics::PhysicsAPI& physicsAPI, NotificationSystem& notificationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_modelRenderer(modelRenderer)
      , m_physicsAPI(physicsAPI)
      , m_registrationMethod(nullptr)
    {
      m_regAnchorModelId = m_modelRenderer.AddModel(REGISTRATION_ANCHOR_MODEL_FILENAME);
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

      if (m_registrationMethod != nullptr && transformContainer != nullptr)
      {
        m_registrationMethod->Update(headPose, coordinateSystem, transformContainer);
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
          m_registrationMethod->SetWorldAnchor(m_regAnchor);
        }
      });
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::IsCameraActive() const
    {
      auto camReg = dynamic_cast<CameraRegistration*>(m_registrationMethod.get());
      return camReg != nullptr && camReg->IsCameraActive();
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"drop anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        m_regAnchorRequested = true;
      };

      callbackMap[L"remove anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        m_registrationMethod->StopAsync().then([this](bool result)
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
        if (dynamic_cast<CameraRegistration*>(m_registrationMethod.get()) != nullptr && m_registrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }
        m_registrationMethod = std::make_shared<CameraRegistration>(m_notificationSystem, m_networkSystem, m_modelRenderer);
        m_registrationMethod->SetWorldAnchor(m_regAnchor);

        m_registrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            m_registrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start camera registration.");
          }
        });
      };

      callbackMap[L"start optical registration"] = [this](SpeechRecognitionResult ^ result)
      {
        if (dynamic_cast<OpticalRegistration*>(m_registrationMethod.get()) != nullptr && m_registrationMethod->IsStarted())
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_regAnchor == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }
        m_registrationMethod = std::make_shared<OpticalRegistration>(m_notificationSystem, m_networkSystem);
        m_registrationMethod->SetWorldAnchor(m_regAnchor);

        m_registrationMethod->StartAsync().then([this](bool result)
        {
          if (!result)
          {
            m_registrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Unable to start optical registration.");
          }
        });
      };

      callbackMap[L"stop registration"] = [this](SpeechRecognitionResult ^ result)
      {
        m_registrationMethod->StopAsync().then([this](bool result)
        {
          if (result)
          {
            m_registrationMethod = nullptr;
            m_notificationSystem.QueueMessage(L"Registration stopped.");
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Error when stopping registration.");
          }
        });
      };

      callbackMap[L"reset registration"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationMethod != nullptr)
        {
          m_registrationMethod->ResetRegistration();
        }
      };

      callbackMap[L"enable registration viz"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationMethod != nullptr)
        {
          m_registrationMethod->EnableVisualization(true);
          m_notificationSystem.QueueMessage(L"Visualization enabled.");
        }
      };

      callbackMap[L"disable registration viz"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationMethod != nullptr)
        {
          m_registrationMethod->EnableVisualization(false);
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

      auto worldAnchor = m_registrationMethod->GetWorldAnchor();
      if (worldAnchor == nullptr)
      {
        return false;
      }

      try
      {
        Platform::IBox<float4x4>^ anchorToRequestedBox = worldAnchor->CoordinateSystem->TryGetTransformTo(requestedCoordinateSystem);
        if (anchorToRequestedBox == nullptr)
        {
          return false;
        }

        outTransform = m_cachedRegistrationTransform * anchorToRequestedBox->Value;
        return true;
      }
      catch (Platform::Exception^ e)
      {
        return false;
      }

      return false;
    }
  }
}