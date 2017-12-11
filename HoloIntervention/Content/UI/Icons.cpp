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
#include "Icons.h"
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
  namespace UI
  {
    const float Icons::ANGLE_BETWEEN_ICONS_RAD = 0.035f;
    const float Icons::ICON_START_ANGLE = 0.225f;
    const float Icons::ICON_UP_ANGLE = 0.1f;
    const float Icons::ICON_SIZE_METER = 0.025f;

    //----------------------------------------------------------------------------
    float3 Icons::GetStabilizedPosition(SpatialPointerPose^ pose) const
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
    float3 Icons::GetStabilizedVelocity() const
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
    float Icons::GetStabilizePriority() const
    {
      return m_componentReady ? PRIORITY_ICON : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    task<bool> Icons::WriteConfigurationAsync(XmlDocument^ document)
    {
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    task<bool> Icons::ReadConfigurationAsync(XmlDocument^ document)
    {
      m_componentReady = true;
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    Icons::Icons(System::NotificationSystem& notificationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
    {
    }

    //----------------------------------------------------------------------------
    Icons::~Icons()
    {
    }

    //----------------------------------------------------------------------------
    void Icons::Update(DX::StepTimer& timer, SpatialPointerPose^ headPose)
    {
      if (!m_componentReady || !m_iconsShowing)
      {
        return;
      }

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
    task<std::shared_ptr<IconEntry>> Icons::AddEntryAsync(const std::wstring& modelName, std::wstring userValue)
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
    task<std::shared_ptr<IconEntry>> Icons::AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, std::wstring userValue)
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
    task<std::shared_ptr<IconEntry>> Icons::AddEntryAsync(const std::wstring& modelName, uint64 userValue /*= 0*/)
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
    task<std::shared_ptr<IconEntry>> Icons::AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, uint64 userValue /*= 0*/)
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
    bool Icons::RemoveEntry(uint64 entryId)
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
    std::shared_ptr<IconEntry> Icons::GetEntry(uint64 entryId)
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
  }
}