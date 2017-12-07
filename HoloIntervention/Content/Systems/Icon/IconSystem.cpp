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
#include "Common.h"
#include "IconEntry.h"
#include "IconSystem.h"
#include "StepTimer.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
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
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const float IconSystem::NETWORK_BLINK_TIME_SEC = 0.75;
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
      pos /= static_cast<float>(m_iconEntries.size()); // Cast size to float, this is safe as entries will be a small positive number
      return pos;
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
      accumulator /= static_cast<float>(m_iconEntries.size()); // Cast size to float, this is safe as entries will be a small positive number
      return accumulator;
    }

    //----------------------------------------------------------------------------
    float IconSystem::GetStabilizePriority() const
    {
      return m_componentReady ? PRIORITY_ICON : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    task<bool> IconSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    task<bool> IconSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      std::vector<task<std::shared_ptr<IconEntry>>> modelLoadingTasks;

      // Create network icons
      for (auto& conn : m_networkSystem.GetConnectors())
      {
        modelLoadingTasks.push_back(AddEntryAsync(L"Assets/Models/network_icon.cmo", conn.HashedName).then([this](std::shared_ptr<IconEntry> entry)
        {
          m_networkLogicEntries.push_back(NetworkLogicEntry());
          return entry;
        }));
      }

      return when_all(begin(modelLoadingTasks), end(modelLoadingTasks)).then([this](std::vector<std::shared_ptr<IconEntry>> entries)
      {
        for (auto& entry : entries)
        {
          m_networkIcons.push_back(entry);
        }
        return AddEntryAsync(L"Assets/Models/microphone_icon.cmo", 0);
      }).then([this](std::shared_ptr<IconEntry> entry)
      {
        m_microphoneIcon = entry;
      }).then([this]()
      {
        // Determine scale factors for all loaded entries
        for (auto& entry : m_iconEntries)
        {
          auto& bounds = entry->GetModelEntry()->GetBounds();
          auto scale = ICON_SIZE_METER / (bounds[1] - bounds[0]);
          entry->SetScaleFactor(scale);
          entry->GetModelEntry()->EnablePoseLerp(true);
          entry->GetModelEntry()->SetPoseLerpRate(8.f);
        }

        m_componentReady = true;
        return true;
      });
    }

    //----------------------------------------------------------------------------
    void IconSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"hide icons"] = [this](SpeechRecognitionResult ^ result)
      {
        for (auto& icon : m_iconEntries)
        {
          icon->GetModelEntry()->SetVisible(false);
        }
        m_iconsShowing = false;
      };

      callbackMap[L"show icons"] = [this](SpeechRecognitionResult ^ result)
      {
        for (auto& icon : m_iconEntries)
        {
          icon->SetFirstFrame(true); // Forces an update to current pose instead of interpolating from last visible point
          icon->GetModelEntry()->SetVisible(true);
        }
        m_iconsShowing = true;
      };
    }

    //----------------------------------------------------------------------------
    IconSystem::IconSystem(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, Input::VoiceInput& voiceInput, Rendering::ModelRenderer& modelRenderer)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_voiceInput(voiceInput)
    {
    }

    //----------------------------------------------------------------------------
    IconSystem::~IconSystem()
    {
    }

    //----------------------------------------------------------------------------
    void IconSystem::Update(DX::StepTimer& timer, SpatialPointerPose^ headPose)
    {
      if (!m_componentReady || !m_iconsShowing)
      {
        return;
      }

      ProcessNetworkLogic(timer);
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
    task<std::shared_ptr<IconEntry>> IconSystem::AddEntryAsync(const std::wstring& modelName, std::wstring userValue)
    {
      return m_modelRenderer.AddModelAsync(modelName).then([this, userValue](uint64 modelId)
      {
        std::lock_guard<std::mutex> guard(m_entryMutex);
        auto modelEntry = m_modelRenderer.GetModel(modelId);
        auto entry = std::make_shared<IconEntry>();
        entry->SetModelEntry(modelEntry);
        entry->GetModelEntry()->EnablePoseLerp(true);
        entry->GetModelEntry()->SetPoseLerpRate(8.f);
        entry->SetUserValue(userValue);
        entry->SetId(m_nextValidEntry++);
        m_iconEntries.push_back(entry);

        return entry;
      });
    }

    //----------------------------------------------------------------------------
    task<std::shared_ptr<IconEntry>> IconSystem::AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, std::wstring userValue)
    {
      // Duplicate incoming model entry, so that they have their own independent rendering properties
      return m_modelRenderer.CloneAsync(modelEntry->GetId()).then([this, userValue](uint64 modelEntryId)
      {
        std::lock_guard<std::mutex> guard(m_entryMutex);
        auto entry = std::make_shared<IconEntry>();
        auto duplicateEntry = m_modelRenderer.GetModel(modelEntryId);
        duplicateEntry->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        entry->SetModelEntry(duplicateEntry);
        entry->GetModelEntry()->EnablePoseLerp(true);
        entry->GetModelEntry()->SetPoseLerpRate(8.f);
        entry->SetUserValue(userValue);
        entry->SetId(m_nextValidEntry++);
        m_iconEntries.push_back(entry);

        return entry;
      });
    }

    //----------------------------------------------------------------------------
    task<std::shared_ptr<IconEntry>> IconSystem::AddEntryAsync(const std::wstring& modelName, uint64 userValue /*= 0*/)
    {
      return m_modelRenderer.AddModelAsync(modelName).then([this, userValue](uint64 modelId)
      {
        std::lock_guard<std::mutex> guard(m_entryMutex);
        auto modelEntry = m_modelRenderer.GetModel(modelId);
        auto entry = std::make_shared<IconEntry>();
        entry->SetModelEntry(modelEntry);
        entry->GetModelEntry()->EnablePoseLerp(true);
        entry->GetModelEntry()->SetPoseLerpRate(8.f);
        entry->SetUserValue(userValue);
        entry->SetId(m_nextValidEntry++);
        m_iconEntries.push_back(entry);

        return entry;
      });
    }

    //----------------------------------------------------------------------------
    task<std::shared_ptr<IconEntry>> IconSystem::AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, uint64 userValue /*= 0*/)
    {
      // Duplicate incoming model entry, so that they have their own independent rendering properties
      return m_modelRenderer.CloneAsync(modelEntry->GetId()).then([this, userValue](uint64 modelEntryId) -> std::shared_ptr<IconEntry>
      {
        if (modelEntryId == INVALID_TOKEN)
        {
          return nullptr;
        }

        std::lock_guard<std::mutex> guard(m_entryMutex);
        auto entry = std::make_shared<IconEntry>();
        auto duplicateEntry = m_modelRenderer.GetModel(modelEntryId);
        entry->SetModelEntry(duplicateEntry);
        entry->GetModelEntry()->EnablePoseLerp(true);
        entry->GetModelEntry()->SetPoseLerpRate(8.f);
        entry->SetUserValue(userValue);
        entry->SetId(m_nextValidEntry++);
        m_iconEntries.push_back(entry);

        return entry;
      });
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
        if (!conn->GetModelEntry()->IsLoaded())
        {
          continue;
        }

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
    void IconSystem::ProcessMicrophoneLogic(DX::StepTimer& timer)
    {
      if (!m_microphoneIcon->GetModelEntry()->IsLoaded())
      {
        return;
      }

      if (!m_wasHearingSound && m_voiceInput.IsHearingSound())
      {
        // Colour!
        m_wasHearingSound = true;
        m_microphoneIcon->GetModelEntry()->SetVisible(true);
        m_microphoneIcon->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
      }
      else if (m_wasHearingSound && !m_voiceInput.IsHearingSound())
      {
        // Greyscale
        m_wasHearingSound = false;
        m_microphoneIcon->GetModelEntry()->SetVisible(true);
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
  }
}