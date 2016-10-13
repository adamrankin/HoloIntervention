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
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::ANCHOR_NAME = ref new Platform::String(L"Registration");
    const std::wstring RegistrationSystem::ANCHOR_MODEL_FILENAME = L"Assets/Models/anchor.cmo";

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer)
      : m_deviceResources(deviceResources)
      , m_stepTimer(stepTimer)
    {
      m_regAnchorModelId = HoloIntervention::instance()->GetModelRenderer().AddModel(ANCHOR_MODEL_FILENAME);
      if (m_regAnchorModelId != Rendering::INVALID_MODEL_ENTRY)
      {
        m_regAnchorModel = HoloIntervention::instance()->GetModelRenderer().GetModel(m_regAnchorModelId);
      }
      if (m_regAnchorModel == nullptr)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to retrieve anchor model.");
        return;
      }
      m_regAnchorModel->SetVisible(false);
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::~RegistrationSystem()
    {
      m_regAnchorModel = nullptr;
      m_regAnchorModelId = 0;
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::Update(SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose)
    {
      // Anchor placement logic
      if (m_regAnchorRequested)
      {
        if (HoloIntervention::instance()->GetSpatialSystem().DropAnchorAtIntersectionHit(ANCHOR_NAME, coordinateSystem, headPose))
        {
          m_regAnchorRequested = false;
          if (m_regAnchorModel != nullptr)
          {
            m_regAnchorModel->SetVisible(true);
          }

          m_networkPCLRegistration->SetSpatialMesh(HoloIntervention::instance()->GetSpatialSystem().GetLastHitMesh());

          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Anchor created.");
        }
      }

      // Anchor position update logic
      if (HoloIntervention::instance()->GetSpatialSystem().HasAnchor(L"Registration"))
      {
        auto transformContainer = HoloIntervention::instance()->GetSpatialSystem().GetAnchor(ANCHOR_NAME)->CoordinateSystem->TryGetTransformTo(coordinateSystem);
        if (transformContainer != nullptr)
        {
          float4x4 anchorToWorld = transformContainer->Value;

          // Coordinate system has orientation and position
          m_regAnchorModel->SetWorld(anchorToWorld);
        }
      }

      if (m_registrationMethod == Registration_NetworkPCL)
      {
        m_networkPCLRegistration->Update(coordinateSystem);
      }
      else if (m_registrationMethod == Registration_CameraPointToLine)
      {
        m_cameraPointToLineRegistration->Update(coordinateSystem);
      }
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task([ = ]()
      {
        if (HoloIntervention::instance()->GetSpatialSystem().HasAnchor(ANCHOR_NAME))
        {
          m_regAnchorModel->SetVisible(true);
        }
      });
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"start camera"] = [this](SpeechRecognitionResult ^ result)
      {
        m_registrationMethod = Registration_CameraPointToLine;
      };

      callbackMap[L"stop camera"] = [this](SpeechRecognitionResult ^ result)
      {

      };

      callbackMap[L"start collecting points"] = [this](SpeechRecognitionResult ^ result)
      {
        m_registrationMethod = Registration_NetworkPCL;
        if (HoloIntervention::instance()->GetIGTLink().IsConnected())
        {
          m_networkPCLRegistration->StartCollectingPoints();
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Collecting points...");
        }
        else
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Not connected!");
        }
      };

      callbackMap[L"end collecting points"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_registrationMethod == Registration_NetworkPCL)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Point collection not active.");
          return;
        }

        m_networkPCLRegistration->EndCollectingPoints();
      };

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
        if (HoloIntervention::instance()->GetSpatialSystem().RemoveAnchor(ANCHOR_NAME) == 1)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Anchor \"" + ANCHOR_NAME + "\" removed.");
        }
      };
    }

    //----------------------------------------------------------------------------
    float4x4 RegistrationSystem::GetRegistrationResult()
    {
      return m_registrationResult;
    }
  }
}