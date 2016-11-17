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
#include "IGTLinkIF.h"

// System includes
#include "SpatialSystem.h"
#include "NotificationSystem.h"

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::REGISTRATION_ANCHOR_NAME = ref new Platform::String(L"Registration");
    const std::wstring RegistrationSystem::REGISTRATION_ANCHOR_MODEL_FILENAME = L"Assets/Models/anchor.cmo";
    const float RegistrationSystem::REGISTRATION_ANCHOR_MODEL_LERP_RATE = 4.f;

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
      , m_cameraRegistration(std::make_shared<CameraRegistration>(deviceResources))
    {
      m_regAnchorModelId = HoloIntervention::instance()->GetModelRenderer().AddModel(REGISTRATION_ANCHOR_MODEL_FILENAME);
      if (m_regAnchorModelId != Rendering::INVALID_ENTRY)
      {
        m_regAnchorModel = HoloIntervention::instance()->GetModelRenderer().GetModel(m_regAnchorModelId);
      }
      if (m_regAnchorModel == nullptr)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to retrieve anchor model.");
        return;
      }
      m_regAnchorModel->SetVisible(false);

      auto repo = ref new UWPOpenIGTLink::TransformRepository();
      auto trName = ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD");
      InitializeTransformRepositoryAsync(repo, L"Assets\\Data\\configuration.xml").then([this, repo, trName]()
      {
        try
        {
          bool isValid;
          m_cachedRegistrationTransform = repo->GetTransform(trName, &isValid);
        }
        catch (Platform::Exception^ e)
        {
          return;
        }
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
        if (HoloIntervention::instance()->GetSpatialSystem().DropAnchorAtIntersectionHit(REGISTRATION_ANCHOR_NAME, coordinateSystem, headPose))
        {
          if (m_regAnchorModel != nullptr)
          {
            m_regAnchorModel->SetVisible(true);
            auto anchor = HoloIntervention::instance()->GetSpatialSystem().GetAnchor(REGISTRATION_ANCHOR_NAME);
            m_cameraRegistration->SetWorldAnchor(anchor);
          }

          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Anchor created.");
        }
        m_regAnchorRequested = false;
      }

      // Anchor model position update logic
      if (HoloIntervention::instance()->GetSpatialSystem().HasAnchor(L"Registration"))
      {
        auto transformContainer = HoloIntervention::instance()->GetSpatialSystem().GetAnchor(REGISTRATION_ANCHOR_NAME)->CoordinateSystem->TryGetTransformTo(coordinateSystem);
        if (transformContainer != nullptr)
        {
          m_anchorModelDesiredWorld = transformContainer->Value;

          const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

          // Use linear interpolation to smooth the pose over time
          float4x4 smoothedPose = lerp(m_anchorModelCurrentWorld, m_anchorModelDesiredWorld, deltaTime * REGISTRATION_ANCHOR_MODEL_LERP_RATE);

          // This will be used as the translation component of the hologram's model transform.
          m_anchorModelCurrentWorld = smoothedPose;

          // Coordinate system has orientation and position
          m_regAnchorModel->SetWorld(m_anchorModelCurrentWorld);
        }
      }

      m_cameraRegistration->Update(coordinateSystem);
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task([ = ]()
      {
        if (HoloIntervention::instance()->GetSpatialSystem().HasAnchor(REGISTRATION_ANCHOR_NAME))
        {
          m_regAnchorModel->SetVisible(true);
        }
      });
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
        if (m_regAnchorModel)
        {
          m_regAnchorModel->SetVisible(false);
        }
        if (HoloIntervention::instance()->GetSpatialSystem().RemoveAnchor(REGISTRATION_ANCHOR_NAME) == 1)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Anchor \"" + REGISTRATION_ANCHOR_NAME + "\" removed.");
        }
      };

      callbackMap[L"start registration"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationActive)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration already running.");
          return;
        }

        if (m_cameraRegistration->GetWorldAnchor() == nullptr)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Anchor required. Please place an anchor with 'drop anchor'.");
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
    }

    //----------------------------------------------------------------------------
    float4x4 RegistrationSystem::GetTrackerToCoordinateSystemTransformation(SpatialCoordinateSystem^ requestedCoordinateSystem)
    {
      auto worldAnchor = m_cameraRegistration->GetWorldAnchor();
      auto trackerToWorldAnchor = m_cameraRegistration->GetTrackerToWorldAnchorTransformation();
      try
      {
        Platform::IBox<float4x4>^ worldAnchorToRequested = worldAnchor->CoordinateSystem->TryGetTransformTo(requestedCoordinateSystem);
        return trackerToWorldAnchor * worldAnchorToRequested->Value;
      }
      catch (Platform::Exception^ e)
      {
        throw std::exception("Unable to relate world anchor to requested coordinate system.");
      }
    }
  }
}