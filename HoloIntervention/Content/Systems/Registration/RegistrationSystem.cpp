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

// Common includes
#include "Common.h"
#include "StepTimer.h"

// Rendering includes
#include "ModelRenderer.h"
#include "ModelEntry.h"

// Spatial includes
#include "SurfaceMesh.h"

// Network includes
#include "IGTConnector.h"

// Physics includes
#include "SurfaceAPI.h"

// System includes
#include "NotificationSystem.h"

// Unnecessary, but removes fake errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::REGISTRATION_ANCHOR_NAME = ref new Platform::String(L"Registration");
    const std::wstring RegistrationSystem::REGISTRATION_ANCHOR_MODEL_FILENAME = L"Assets/Models/anchor.cmo";

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedPosition() const
    {
      if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return (m_cameraRegistration->GetStabilizedPosition() + transform(float3(0.f, 0.f, 0.f), m_regAnchorModel->GetWorld())) / 2.f;
      }
      else if (!m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return transform(float3(0.f, 0.f, 0.f), m_regAnchorModel->GetWorld());
      }
      else if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId == INVALID_TOKEN)
      {
        // This shouldn't be possible, camReg needs anchor
        assert(false);
        return m_cameraRegistration->GetStabilizedPosition();
      }
      else
      {
        // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
        assert(false);
        return float3(0.f, 0.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedNormal() const
    {
      if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return (m_cameraRegistration->GetStabilizedNormal() + ExtractNormal(m_regAnchorModel->GetWorld())) / 2.f;
      }
      else if (!m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return ExtractNormal(m_regAnchorModel->GetWorld());
      }
      else if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId == INVALID_TOKEN)
      {
        // This shouldn't be possible, camReg needs anchor
        assert(false);
        return m_cameraRegistration->GetStabilizedNormal();
      }
      else
      {
        // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
        assert(false);
        return float3(0.f, 1.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    float3 RegistrationSystem::GetStabilizedVelocity() const
    {
      if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return (m_cameraRegistration->GetStabilizedVelocity() + m_regAnchorModel->GetVelocity()) / 2.f;
      }
      else if (!m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        return m_regAnchorModel->GetVelocity();
      }
      else if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId == INVALID_TOKEN)
      {
        // This shouldn't be possible, camReg needs anchor
        assert(false);
        return m_cameraRegistration->GetStabilizedVelocity();
      }
      else
      {
        // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
        assert(false);
        return float3(0.f, 0.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    float RegistrationSystem::GetStabilizePriority() const
    {
      if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        // TODO : stabilization values?
        return std::fmaxf(m_cameraRegistration->GetStabilizePriority(), 3.f);
      }
      else if (!m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId != INVALID_TOKEN)
      {
        // TODO : stabilization values?
        return 3.f;
      }
      else if (m_cameraRegistration->IsStabilizationActive() && m_regAnchorModelId == INVALID_TOKEN)
      {
        // This shouldn't be possible, camReg needs anchor
        assert(false);
        return m_cameraRegistration->GetStabilizePriority();
      }
      else
      {
        // Nothing completed yet, this shouldn't even be called because in this case, priority returns not active
        assert(false);
        return PRIORITY_NOT_ACTIVE;
      }
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(Network::IGTConnector& igtConnector, Physics::SurfaceAPI& physicsAPI, NotificationSystem& notificationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_modelRenderer(modelRenderer)
      , m_physicsAPI(physicsAPI)
      , m_cameraRegistration(std::make_shared<CameraRegistration>(notificationSystem, igtConnector, modelRenderer))
    {
      m_cameraRegistration->SetVisualization(true);

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

      auto repo = ref new UWPOpenIGTLink::TransformRepository();
      auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD");
      InitializeTransformRepositoryAsync(repo, L"Assets\\Data\\configuration.xml").then([this, repo, trName]()
      {
        try
        {
          m_cachedRegistrationTransform = transpose(repo->GetTransform(trName));
        }
        catch (Platform::Exception^ e)
        {
          return;
        }
      }).then([this]()
      {
        m_componentReady = true;
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
            m_cameraRegistration->SetWorldAnchor(m_regAnchor);
          }

          m_notificationSystem.QueueMessage(L"Anchor created.");
        }
        m_regAnchorRequested = false;
      }

      if (m_cameraRegistration->HasRegistration())
      {
        m_cachedRegistrationTransform = m_cameraRegistration->GetReferenceToWorldAnchorTransformation();
      }

      Platform::IBox<float4x4>^ transformContainer(nullptr);
      // Anchor model position update logic
      if (m_regAnchor != nullptr)
      {
        transformContainer = m_regAnchor->CoordinateSystem->TryGetTransformTo(coordinateSystem);
        if (transformContainer != nullptr)
        {
          m_regAnchorModel->SetWorld(transformContainer->Value);
        }
      }

      m_cameraRegistration->Update(transformContainer);
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task([ = ]()
      {
        if (m_physicsAPI.HasAnchor(REGISTRATION_ANCHOR_NAME))
        {
          m_regAnchor = m_physicsAPI.GetAnchor(REGISTRATION_ANCHOR_NAME);
          m_regAnchorModel->SetVisible(true);
        }
      });
    }

    //----------------------------------------------------------------------------
    bool RegistrationSystem::IsCameraActive() const
    {
      return m_cameraRegistration->IsCameraActive();
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"drop anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        m_regAnchorRequested = true;
        if (m_registrationActive)
        {
          m_registrationActive = false;
        }
      };

      callbackMap[L"remove anchor"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_regAnchorModel)
        {
          m_regAnchorModel->SetVisible(false);
        }
        if (m_physicsAPI.RemoveAnchor(REGISTRATION_ANCHOR_NAME) == 1)
        {
          m_notificationSystem.QueueMessage(L"Anchor \"" + REGISTRATION_ANCHOR_NAME + "\" removed.");
        }
      };

      callbackMap[L"start registration"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationActive)
        {
          m_notificationSystem.QueueMessage(L"Registration already running.");
          return;
        }

        if (m_cameraRegistration->GetWorldAnchor() == nullptr)
        {
          m_notificationSystem.QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
          return;
        }

        m_cameraRegistration->StartCameraAsync().then([this](bool result)
        {
          if (!result)
          {
            return;
          }
          m_registrationActive = true;
        });
      };

      callbackMap[L"stop registration"] = [this](SpeechRecognitionResult ^ result)
      {
        m_cameraRegistration->StopCameraAsync().then([this](bool result)
        {
          if (result)
          {
            m_registrationActive = false;
          }
        });
      };

      callbackMap[L"enable spheres"] = [this](SpeechRecognitionResult ^ result)
      {
        m_cameraRegistration->SetVisualization(true);
        m_notificationSystem.QueueMessage(L"Sphere visualization enabled.");
      };

      callbackMap[L"disable spheres"] = [this](SpeechRecognitionResult ^ result)
      {
        m_cameraRegistration->SetVisualization(false);
        m_notificationSystem.QueueMessage(L"Sphere visualization disabled.");
      };
    }

    //----------------------------------------------------------------------------
    float4x4 RegistrationSystem::GetReferenceToCoordinateSystemTransformation(SpatialCoordinateSystem^ requestedCoordinateSystem)
    {
      if (m_cachedRegistrationTransform == float4x4::identity())
      {
        throw std::exception("Registration not completed.");
      }

      auto worldAnchor = m_cameraRegistration->GetWorldAnchor();
      if (worldAnchor == nullptr)
      {
        throw std::exception("World anchor doesn't exist.");
      }

      try
      {
        Platform::IBox<float4x4>^ anchorToRequestedBox = worldAnchor->CoordinateSystem->TryGetTransformTo(requestedCoordinateSystem);
        if (anchorToRequestedBox == nullptr)
        {
          throw std::exception("AnchorToCoordSystem IBox is empty.");
        }
        return m_cachedRegistrationTransform * anchorToRequestedBox->Value;
      }
      catch (Platform::Exception^ e)
      {
        throw std::exception("Unable to relate world anchor to requested coordinate system.");
      }
    }
  }
}