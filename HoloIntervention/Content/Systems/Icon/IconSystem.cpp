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
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "ToolSystem.h"
#include "ToolEntry.h"

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
    float3 IconSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      float3 pos = { 0.f, 0.f, 0.f };
      for (auto& icon : m_iconEntries)
      {
        auto pose = icon->GetModelEntry()->GetCurrentPose();
        pos.x += pose.m41;
        pos.y += pose.m42;
        pos.z += pose.m43;
      }
      pos /= m_iconEntries.size();
      return pos;
    }

    //----------------------------------------------------------------------------
    float3 IconSystem::GetStabilizedNormal(SpatialPointerPose^ pose) const
    {
      float3 accumulator = { 0.f, 0.f, 0.f };
      for (auto& icon : m_iconEntries)
      {
        auto normal = ExtractNormal(icon->GetModelEntry()->GetCurrentPose());
        accumulator.x += normal.x;
        accumulator.y += normal.y;
        accumulator.z += normal.z;
      }
      accumulator /= m_iconEntries.size();
      return accumulator;
    }

    //----------------------------------------------------------------------------
    float3 IconSystem::GetStabilizedVelocity() const
    {
      float3 accumulator = { 0.f, 0.f, 0.f };
      for (auto& icon : m_iconEntries)
      {
        auto vel = icon->GetModelEntry()->GetVelocity();
        accumulator.x += vel.x;
        accumulator.y += vel.y;
        accumulator.z += vel.z;
      }
      accumulator /= m_iconEntries.size();
      return accumulator;
    }

    //----------------------------------------------------------------------------
    float IconSystem::GetStabilizePriority() const
    {
      // TODO : stabilizer values?
      return 0.5f;
    }

    //----------------------------------------------------------------------------
    IconSystem::IconSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, NetworkSystem& networkSystem, ToolSystem& toolSystem, Input::VoiceInput& voiceInput, Rendering::ModelRenderer& modelRenderer)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_networkSystem(networkSystem)
      , m_toolSystem(toolSystem)
      , m_voiceInput(voiceInput)
    {
      // Create network icons
      for (auto& conn : m_networkSystem.GetConnectors())
      {
        m_networkIcons.push_back(AddEntry(L"Assets/Models/network_icon.cmo", conn.HashedName));
        m_networkLogicEntries.push_back(NetworkLogicEntry());
      }

      // Create camera icon
      m_cameraIcon = AddEntry(L"Assets/Models/camera_icon.cmo", 0);

      // Create microphone icon
      m_microphoneIcon = AddEntry(L"Assets/Models/microphone_icon.cmo", 0);

      // Create tool icons
      for (auto& tool : m_toolSystem.GetTools())
      {
        auto entry = AddEntry(tool->GetModelEntry(), std::wstring(tool->GetCoordinateFrame()->GetTransformName()->Data()));
        entry->SetUserValue(tool->GetId());
        m_toolIcons.push_back(entry);
      }

      create_task([this]()
      {
        if (!wait_until_condition([this]()
      {
        bool loaded = m_cameraIcon->GetModelEntry()->IsLoaded() && m_microphoneIcon->GetModelEntry()->IsLoaded();
          for (auto& conn : m_networkIcons)
          {
            loaded &= conn->GetModelEntry()->IsLoaded();
          }
          for (auto& icon : m_toolIcons)
          {
            loaded &= icon->GetModelEntry()->IsLoaded();
          }
          return loaded;
        }, 5000))
        {
          m_notificationSystem.QueueMessage(L"Icon models failed to load after 5s.");
          return false;
        }

        // Determine scale factors for the models
        for (auto& conn : m_networkIcons)
        {
          auto& bounds = conn->GetModelEntry()->GetBounds();
          auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          conn->SetScaleFactor(scale);
        }

        auto& bounds = m_cameraIcon->GetModelEntry()->GetBounds();
        auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
        m_cameraIcon->SetScaleFactor(scale);

        bounds = m_microphoneIcon->GetModelEntry()->GetBounds();
        scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
        m_microphoneIcon->SetScaleFactor(scale);

        for (auto& tool : m_toolIcons)
        {
          bounds = tool->GetModelEntry()->GetBounds();
          scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          tool->SetScaleFactor(scale);
        }

        return true;
      }).then([this](bool loaded)
      {
        for (auto& conn : m_networkIcons)
        {
          conn->GetModelEntry()->EnablePoseLerp(true);
          conn->GetModelEntry()->SetPoseLerpRate(8.f);
          m_iconEntries.push_back(conn);
        }

        m_cameraIcon->GetModelEntry()->EnablePoseLerp(true);
        m_cameraIcon->GetModelEntry()->SetPoseLerpRate(8.f);

        m_microphoneIcon->GetModelEntry()->EnablePoseLerp(true);
        m_microphoneIcon->GetModelEntry()->SetPoseLerpRate(8.f);

        m_iconEntries.push_back(m_cameraIcon);
        m_iconEntries.push_back(m_microphoneIcon);

        for (auto& tool : m_toolIcons)
        {
          tool->GetModelEntry()->EnablePoseLerp(true);
          tool->GetModelEntry()->SetPoseLerpRate(8.f);
          m_iconEntries.push_back(tool);
        }

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
    std::shared_ptr<IconEntry> IconSystem::AddEntry(const std::wstring& modelName, std::wstring userValue)
    {
      auto modelEntryId = m_modelRenderer.AddModel(modelName);
      auto modelEntry = m_modelRenderer.GetModel(modelEntryId);
      auto entry = std::make_shared<IconEntry>();
      entry->SetModelEntry(modelEntry);
      entry->SetUserValue(userValue);
      entry->SetId(m_nextValidEntry++);

      return entry;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::System::IconEntry> IconSystem::AddEntry(std::shared_ptr<Rendering::ModelEntry> modelEntry, std::wstring userValue)
    {
      auto entry = std::make_shared<IconEntry>();
      entry->SetModelEntry(modelEntry);
      entry->SetUserValue(userValue);
      entry->SetId(m_nextValidEntry++);

      return entry;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::System::IconEntry> IconSystem::AddEntry(const std::wstring& modelName, uint64 userValue /*= 0*/)
    {
      auto modelEntryId = m_modelRenderer.AddModel(modelName);
      auto modelEntry = m_modelRenderer.GetModel(modelEntryId);
      auto entry = std::make_shared<IconEntry>();
      entry->SetModelEntry(modelEntry);
      entry->SetUserValue(userValue);
      entry->SetId(m_nextValidEntry++);

      return entry;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::System::IconEntry> IconSystem::AddEntry(std::shared_ptr<Rendering::ModelEntry> modelEntry, uint64 userValue /*= 0*/)
    {
      auto entry = std::make_shared<IconEntry>();
      entry->SetModelEntry(modelEntry);
      entry->SetUserValue(userValue);
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
      for (uint32 i = 0; i < m_networkIcons.size(); ++i)
      {
        auto conn = m_networkIcons[i];

        NetworkSystem::ConnectionState state;
        if (!m_networkSystem.GetConnectionState(conn->GetUserValueNumber(), state))
        {
          return;
        }

        switch (state)
        {
          case NetworkSystem::CONNECTION_STATE_CONNECTING:
          case NetworkSystem::CONNECTION_STATE_DISCONNECTING:
            if (m_networkLogicEntries[i].m_networkPreviousState != state)
            {
              m_networkLogicEntries[i].m_networkBlinkTimer = 0.f;
            }
            else
            {
              m_networkLogicEntries[i].m_networkBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
              if (m_networkLogicEntries[i].m_networkBlinkTimer >= NETWORK_BLINK_TIME_SEC)
              {
                m_networkLogicEntries[i].m_networkBlinkTimer = 0.f;
                conn->GetModelEntry()->ToggleVisible();
              }
            }
            m_networkLogicEntries[i].m_networkIsBlinking = true;
            break;
          case NetworkSystem::CONNECTION_STATE_UNKNOWN:
          case NetworkSystem::CONNECTION_STATE_DISCONNECTED:
          case NetworkSystem::CONNECTION_STATE_CONNECTION_LOST:
            conn->GetModelEntry()->SetVisible(true);
            m_networkLogicEntries[i].m_networkIsBlinking = false;
            if (m_networkLogicEntries[i].m_wasNetworkConnected)
            {
              conn->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
              m_networkLogicEntries[i].m_wasNetworkConnected = false;
            }
            break;
          case NetworkSystem::CONNECTION_STATE_CONNECTED:
            conn->GetModelEntry()->SetVisible(true);
            m_networkLogicEntries[i].m_networkIsBlinking = false;
            if (!m_networkLogicEntries[i].m_wasNetworkConnected)
            {
              m_networkLogicEntries[i].m_wasNetworkConnected = true;
              conn->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
            }
            break;
        }

        m_networkLogicEntries[i].m_networkPreviousState = state;
      }
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessCameraLogic(DX::StepTimer& timer)
    {
      if (!m_wasCameraOn && m_registrationSystem.IsCameraActive())
      {
        m_wasCameraOn = true;
        m_cameraIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
      }
      else if (m_wasCameraOn && !m_registrationSystem.IsCameraActive())
      {
        m_wasCameraOn = false;
        m_cameraIcon->GetModelEntry()->SetVisible(true);
        m_cameraIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
      }
      else if (m_wasCameraOn && m_registrationSystem.IsCameraActive())
      {
        m_cameraBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
        if (m_cameraBlinkTimer >= NETWORK_BLINK_TIME_SEC)
        {
          m_cameraBlinkTimer = 0.f;
          m_cameraIcon->GetModelEntry()->ToggleVisible();
        }
      }
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessMicrophoneLogic(DX::StepTimer& timer)
    {
      if (!m_wasHearingSound && m_voiceInput.IsHearingSound())
      {
        // Colour!
        m_wasHearingSound = true;
        m_microphoneIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
      }
      else if (m_wasHearingSound && !m_voiceInput.IsHearingSound())
      {
        // Greyscale
        m_wasHearingSound = false;
        m_microphoneIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
      }
      else if (m_wasHearingSound && m_voiceInput.IsHearingSound())
      {
        // Blink!
        m_microphoneBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
        if (m_microphoneBlinkTimer >= MICROPHONE_BLINK_TIME_SEC)
        {
          m_microphoneBlinkTimer = 0.f;
          m_microphoneIcon->GetModelEntry()->ToggleVisible();
        }
      }
    }

    //----------------------------------------------------------------------------
    void IconSystem::ProcessToolLogic(DX::StepTimer&)
    {
      for (auto& tool : m_toolIcons)
      {
        auto coordFrame = tool->GetUserValueString();
        auto id = tool->GetUserValueNumber();
        auto isValid = m_toolSystem.IsToolValid(id);
        auto wasValid = m_toolSystem.WasToolValid(id);
        if (isValid && !wasValid)
        {
          tool->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
        }
        else if (!isValid && wasValid)
        {
          tool->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        }
      }
    }
  }
}