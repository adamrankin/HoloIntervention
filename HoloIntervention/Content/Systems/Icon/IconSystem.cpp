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
#include "IconEntry.h"
#include "IconSystem.h"

// System includes
#include "NotificationSystem.h"

// Rendering includes
#include "ModelRenderer.h"

// Unnecessary, but removes intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const float IconSystem::ANGLE_BETWEEN_ICONS_DEG = 5.75f;
    const float IconSystem::ICON_SIZE_METER = 0.15f;

    //----------------------------------------------------------------------------
    IconSystem::IconSystem()
    {
      // Create network icon
      m_networkIcon = AddEntry(L"Assets/Models/network_icon.cmo");

      // Create camera icon
      m_cameraIcon = AddEntry(L"Assets/Models/camera_icon.cmo");

      create_task([this]()
      {
        uint32 msCount(0);
        while (!m_networkIcon->GetModelEntry()->IsLoaded() && !m_cameraIcon->GetModelEntry()->IsLoaded() && msCount < 5000)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          msCount += 100;
        }

        if (msCount >= 5000)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Icon models failed to load after 5s.");
          return false;
        }

        // Determine scale factors for both models
        {
          auto& bounds = m_networkIcon->GetModelEntry()->GetBounds();
          m_networkIcon->SetScaleFactor(ICON_SIZE_METER / (bounds[1] - bounds[0]));
        }

        {
          auto& bounds = m_cameraIcon->GetModelEntry()->GetBounds();
          m_cameraIcon->SetScaleFactor(ICON_SIZE_METER / (bounds[1] - bounds[0]));
        }

        return true;
      }).then([this](bool loaded)
      {
        m_iconEntries.push_back(m_networkIcon);
        m_iconEntries.push_back(m_cameraIcon);
        m_componentReady = loaded;
      });
    }

    //----------------------------------------------------------------------------
    IconSystem::~IconSystem()
    {
    }

    //----------------------------------------------------------------------------
    void IconSystem::Update(DX::StepTimer& timer, SpatialCoordinateSystem^ renderingCoordinateSystem, SpatialPointerPose^ headPose)
    {
      if (!m_componentReady)
      {
        return;
      }

      // Calculate forward vector 2m ahead
      float3 basePosition = headPose->Head->Position + (2.f * headPose->Head->ForwardDirection);
      float4x4 rotatedToWorldPose = make_float4x4_world(basePosition, -headPose->Head->ForwardDirection, float3(0.f, 1.f, 0.f));

      int32 i = 0;
      const float PI = 3.14159265359f;
      float angle = -0.25f * PI;
      for (auto& entry : m_iconEntries)
      {
        float4x4 worldToScaledWorldPose = make_float4x4_scale(entry->GetScaleFactor());
        entry->GetModelEntry()->SetWorld(make_float4x4_rotation_y(angle) * rotatedToWorldPose * worldToScaledWorldPose);
        angle += (ANGLE_BETWEEN_ICONS_DEG / 180.f * PI);
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<IconEntry> IconSystem::AddEntry(const std::wstring& modelName)
    {
      auto modelEntryId = HoloIntervention::instance()->GetModelRenderer().AddModel(modelName);
      auto modelEntry = HoloIntervention::instance()->GetModelRenderer().GetModel(modelEntryId);
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
  }
}