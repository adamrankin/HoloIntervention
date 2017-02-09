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
#include "Common.h"
#include "IconEntry.h"
#include "IconSystem.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// Input includes
#include "VoiceInput.h"

// Rendering includes
#include "ModelRenderer.h"

// Unnecessary, but removes intellisense errors
#include <WindowsNumerics.h>
#include "Log.h"

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const float IconSystem::NETWORK_BLINK_TIME_SEC = 0.75;
    const float IconSystem::CAMERA_BLINK_TIME_SEC = 1.25f;
    const float IconSystem::MICROPHONE_BLINK_TIME_SEC = 1.f;
    const float IconSystem::ANGLE_BETWEEN_ICONS_RAD = 0.035f;
    const float IconSystem::ICON_START_ANGLE = 0.225f;
    const float IconSystem::ICON_UP_ANGLE = 0.1f;
    const float IconSystem::ICON_SIZE_METER = 0.025f;

    //----------------------------------------------------------------------------
    float3 IconSystem::GetStabilizedPosition() const
    {
      return (transform(float3(0.f, 0.f, 0.f), m_networkIcon->GetModelEntry()->GetCurrentPose()) + transform(float3(0.f, 0.f, 0.f), m_cameraIcon->GetModelEntry()->GetCurrentPose())) / 2.f;
    }

    //----------------------------------------------------------------------------
    float3 IconSystem::GetStabilizedNormal() const
    {
      return (ExtractNormal(m_networkIcon->GetModelEntry()->GetCurrentPose()) + ExtractNormal(m_cameraIcon->GetModelEntry()->GetCurrentPose())) / 2.f;
    }

    //----------------------------------------------------------------------------
    float3 IconSystem::GetStabilizedVelocity() const
    {
      return (m_networkIcon->GetModelEntry()->GetVelocity() + m_networkIcon->GetModelEntry()->GetVelocity()) / 2.f;
    }

    //----------------------------------------------------------------------------
    float IconSystem::GetStabilizePriority() const
    {
      // TODO : stabilizer values?
      return 0.5f;
    }

    //----------------------------------------------------------------------------
    IconSystem::IconSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Network::IGTConnector& igtConnector, Input::VoiceInput& voiceInput, Rendering::ModelRenderer& modelRenderer)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_IGTConnector(igtConnector)
      , m_voiceInput(voiceInput)
    {
      // Create network icon
      m_networkIcon = AddEntry(L"Assets/Models/network_icon.cmo");

      // Create camera icon
      m_cameraIcon = AddEntry(L"Assets/Models/camera_icon.cmo");

      // Create microphone icon
      m_microphoneIcon = AddEntry(L"Assets/Models/microphone_icon.cmo");

      create_task([this]()
      {
        if (!wait_until_condition([this]() {return m_networkIcon->GetModelEntry()->IsLoaded() && m_cameraIcon->GetModelEntry()->IsLoaded() && m_microphoneIcon->GetModelEntry()->IsLoaded();}, 5000))
        {
          m_notificationSystem.QueueMessage(L"Icon models failed to load after 5s.");
          return false;
        }

        // Determine scale factors for both models
        {
          auto& bounds = m_networkIcon->GetModelEntry()->GetBounds();
          auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          m_networkIcon->SetScaleFactor(scale);
        }

        {
          auto& bounds = m_cameraIcon->GetModelEntry()->GetBounds();
          auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          m_cameraIcon->SetScaleFactor(scale);
        }

        {
          auto& bounds = m_microphoneIcon->GetModelEntry()->GetBounds();
          auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          m_microphoneIcon->SetScaleFactor(scale);
        }

        return true;
      }).then([this](bool loaded)
      {
        m_networkIcon->GetModelEntry()->EnablePoseLerp(true);
        m_networkIcon->GetModelEntry()->SetPoseLerpRate(8.f);
        m_cameraIcon->GetModelEntry()->EnablePoseLerp(true);
        m_cameraIcon->GetModelEntry()->SetPoseLerpRate(8.f);
        m_microphoneIcon->GetModelEntry()->EnablePoseLerp(true);
        m_microphoneIcon->GetModelEntry()->SetPoseLerpRate(8.f);

        m_iconEntries.push_back(m_networkIcon);
        m_iconEntries.push_back(m_cameraIcon);
        m_iconEntries.push_back(m_microphoneIcon);
        m_componentReady = loaded;
      });
    }

    //----------------------------------------------------------------------------
    IconSystem::~IconSystem()
    {
    }

    //----------------------------------------------------------------------------
    void IconSystem::Update(DX::StepTimer& timer, SpatialPointerPose^ headPose)
    {
      if (!m_componentReady)
      {
        return;
      }

      ProcessNetworkLogic(timer);
      ProcessCameraLogic(timer);
      ProcessMicrophoneLogic(timer);

      // Calculate forward vector 2m ahead
      float3 basePosition = headPose->Head->Position + (float3(2.f) * headPose->Head->ForwardDirection);
      float4x4 translation = make_float4x4_translation(basePosition);
      uint32 i = 0;
      for (auto& entry : m_iconEntries)
      {
        float4x4 scale = make_float4x4_scale(entry->GetScaleFactor());
        float4x4 rotate = make_float4x4_from_axis_angle(headPose->Head->UpDirection, ICON_START_ANGLE - i * ANGLE_BETWEEN_ICONS_RAD) * make_float4x4_from_axis_angle(cross(headPose->Head->UpDirection, -headPose->Head->ForwardDirection), ICON_UP_ANGLE);
        float4x4 transformed = translation * rotate;
        float4x4 world = make_float4x4_world(float3(transformed.m41, transformed.m42, transformed.m43), headPose->Head->ForwardDirection, headPose->Head->UpDirection);

        if (entry->GetFirstFrame())
        {
          entry->GetModelEntry()->SetCurrentPose(scale * world);
          entry->SetFirstFrame(false);
        }
        else
        {
          entry->GetModelEntry()->SetDesiredPose(scale * world);
        }

        ++i;
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<IconEntry> IconSystem::AddEntry(const std::wstring& modelName)
    {
      auto modelEntryId = m_modelRenderer.AddModel(modelName);
      auto modelEntry = m_modelRenderer.GetModel(modelEntryId);
      auto entry = std::make_shared<IconEntry>();
      entry->SetModelEntry(modelEntry);
      entry->SetId(m_nextValidEntry++);

      return entry;
    }

    //----------------------------------------------------------------------------
    bool IconSystem::RemoveEntry(uint64 entryId)
    {
      for (auto it = m_iconEntries.begin(); it != m_iconEntries.end(); ++it)
      {
        if ((*it)->GetId() == entryId)
        {
          m_iconEntries.erase(it);
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::System::IconEntry> IconSystem::GetEntry(uint64 entryId)
    {
      for (auto& entry : m_iconEntries)
      {
        if (entry->GetId() == entryId)
        {
          return entry;
        }
      }

      return nullptr;
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessNetworkLogic(DX::StepTimer& timer)
    {
      auto state = m_IGTConnector.GetConnectionState();

      switch (state)
      {
      case HoloIntervention::Network::CONNECTION_STATE_CONNECTING:
      case HoloIntervention::Network::CONNECTION_STATE_DISCONNECTING:
        if (m_networkPreviousState != state)
        {
          m_networkBlinkTimer = 0.f;
        }
        else
        {
          m_networkBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
          if (m_networkBlinkTimer >= NETWORK_BLINK_TIME_SEC)
          {
            m_networkBlinkTimer = 0.f;
            m_networkIcon->GetModelEntry()->ToggleVisible();
          }
        }
        m_networkIsBlinking = true;
        if (state == Network::CONNECTION_STATE_CONNECTING)
        {
          m_networkRenderState = Rendering::RENDERING_GREYSCALE;
        }
        else
        {
          m_networkRenderState = Rendering::RENDERING_DEFAULT;
        }
        break;
      case HoloIntervention::Network::CONNECTION_STATE_UNKNOWN:
      case HoloIntervention::Network::CONNECTION_STATE_DISCONNECTED:
      case HoloIntervention::Network::CONNECTION_STATE_CONNECTION_LOST:
        m_networkIcon->GetModelEntry()->SetVisible(true);
        m_networkIsBlinking = false;
        m_networkRenderState = Rendering::RENDERING_GREYSCALE;
        break;
      case HoloIntervention::Network::CONNECTION_STATE_CONNECTED:
        m_networkIcon->GetModelEntry()->SetVisible(true);
        m_networkIsBlinking = false;
        m_networkRenderState = Rendering::RENDERING_DEFAULT;
        break;
      }

      m_networkIcon->GetModelEntry()->SetRenderingState(m_networkRenderState);
      m_networkPreviousState = state;
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessCameraLogic(DX::StepTimer& timer)
    {
      if (m_registrationSystem.IsCameraActive())
      {
        m_cameraIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
        m_cameraBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
        if (m_cameraBlinkTimer >= NETWORK_BLINK_TIME_SEC)
        {
          m_cameraBlinkTimer = 0.f;
          m_cameraIcon->GetModelEntry()->ToggleVisible();
        }
      }
      else
      {
        m_cameraIcon->GetModelEntry()->SetVisible(true);
        m_cameraIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
      }
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessMicrophoneLogic(DX::StepTimer& timer)
    {
      if (m_voiceInput.IsRecognitionActive())
      {
        // TODO : microphone model, animation?
      }
      else
      {
        m_networkIcon->GetModelEntry()->SetVisible(true);
        m_networkIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
      }
    }
  }
}