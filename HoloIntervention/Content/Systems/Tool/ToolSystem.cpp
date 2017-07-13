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

#pragma once

// Local includes
#include "pch.h"
#include "ToolEntry.h"
#include "ToolSystem.h"
#include "Common.h"

// Rendering includes
#include "ModelRenderer.h"

// System includes
#include "RegistrationSystem.h"
#include "NotificationSystem.h"
#include "NetworkSystem.h"

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    float3 ToolSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      std::shared_ptr<Tools::ToolEntry> maxEntry(nullptr);
      float maxPriority(PRIORITY_NOT_ACTIVE);
      for (auto& entry : m_toolEntries)
      {
        if (entry->GetStabilizePriority() > maxPriority)
        {
          maxPriority = entry->GetStabilizePriority();
          maxEntry = entry;
        }
      }

      if (maxEntry != nullptr)
      {
        return maxEntry->GetStabilizedPosition(pose);
      }

      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 ToolSystem::GetStabilizedVelocity() const
    {
      std::shared_ptr<Tools::ToolEntry> maxEntry(nullptr);
      float maxPriority(PRIORITY_NOT_ACTIVE);
      for (auto& entry : m_toolEntries)
      {
        if (entry->GetStabilizePriority() > maxPriority)
        {
          maxPriority = entry->GetStabilizePriority();
          maxEntry = entry;
        }
      }

      if (maxEntry != nullptr)
      {
        return maxEntry->GetStabilizedVelocity();
      }

      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float ToolSystem::GetStabilizePriority() const
    {
      float maxPriority(PRIORITY_NOT_ACTIVE);
      for (auto& entry : m_toolEntries)
      {
        if (entry->GetStabilizePriority() > maxPriority)
        {
          maxPriority = entry->GetStabilizePriority();
        }
      }

      return maxPriority;
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> ToolSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention");
        if (document->SelectNodes(xpath)->Length != 1)
        {
          return false;
        }

        auto node = document->SelectNodes(xpath)->Item(0);

        auto toolsElem = document->CreateElement("Tools");
        toolsElem->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));
        node->AppendChild(toolsElem);

        for (auto tool : m_toolEntries)
        {
          auto toolElem = document->CreateElement("Tool");
          toolElem->SetAttribute(L"Model", ref new Platform::String(tool->GetModelEntry()->GetAssetLocation().c_str()));
          toolElem->SetAttribute(L"From", tool->GetCoordinateFrame()->From());
          toolElem->SetAttribute(L"To", tool->GetCoordinateFrame()->To());
          toolElem->SetAttribute(L"LerpEnabled", tool->GetModelEntry()->GetLerpEnabled() ? L"true" : L"false");
          if (tool->GetModelEntry()->GetLerpEnabled())
          {
            toolElem->SetAttribute(L"LerpRate", tool->GetModelEntry()->GetLerpRate().ToString());
          }
          toolsElem->AppendChild(toolElem);
        }

        m_transformRepository->WriteConfiguration(document);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> ToolSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      if (!m_transformRepository->ReadConfiguration(document))
      {
        return task_from_result(false);
      }

      return InitAsync(document).then([this]()
      {
        m_componentReady = true;
        return true;
      });
    }

    //----------------------------------------------------------------------------
    ToolSystem::ToolSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer, NetworkSystem& networkSystem)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_modelRenderer(modelRenderer)
      , m_transformRepository(ref new UWPOpenIGTLink::TransformRepository())
      , m_networkSystem(networkSystem)
    {
    }

    //----------------------------------------------------------------------------
    ToolSystem::~ToolSystem()
    {
    }

    //----------------------------------------------------------------------------
    uint32 ToolSystem::GetToolCount() const
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      return m_toolEntries.size();
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Tools::ToolEntry> ToolSystem::GetTool(uint64 token) const
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto iter = m_toolEntries.begin(); iter != m_toolEntries.end(); ++iter)
      {
        if (token == (*iter)->GetId())
        {
          return *iter;
        }
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    std::vector<std::shared_ptr<HoloIntervention::Tools::ToolEntry>> ToolSystem::GetTools()
    {
      return m_toolEntries;
    }

    //----------------------------------------------------------------------------
    bool ToolSystem::IsToolValid(uint64 token) const
    {
      auto tool = GetTool(token);
      if (tool == nullptr)
      {
        return false;
      }

      return tool->IsValid();
    }

    //----------------------------------------------------------------------------
    bool ToolSystem::WasToolValid(uint64 token) const
    {
      auto tool = GetTool(token);
      if (tool == nullptr)
      {
        return false;
      }

      return tool->WasValid();
    }

    //----------------------------------------------------------------------------
    task<uint64> ToolSystem::RegisterToolAsync(const std::wstring& modelName, UWPOpenIGTLink::TransformName^ coordinateFrame)
    {
      return m_modelRenderer.AddModelAsync(modelName).then([this, coordinateFrame](uint64 modelEntryId)
      {
        std::shared_ptr<Tools::ToolEntry> entry = std::make_shared<Tools::ToolEntry>(m_modelRenderer, m_networkSystem, m_hashedConnectionName, coordinateFrame, m_transformRepository);
        auto modelEntry = m_modelRenderer.GetModel(modelEntryId);
        entry->SetModelEntry(modelEntry);
        modelEntry->SetVisible(false);
        std::lock_guard<std::mutex> guard(m_entriesMutex);
        m_toolEntries.push_back(entry);
        return entry->GetId();
      });
    }

    //----------------------------------------------------------------------------
    void ToolSystem::UnregisterTool(uint64 toolToken)
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto iter = m_toolEntries.begin(); iter != m_toolEntries.end(); ++iter)
      {
        if (toolToken == (*iter)->GetId())
        {
          m_toolEntries.erase(iter);
          return;
        }
      }
    }

    //----------------------------------------------------------------------------
    void ToolSystem::ClearTools()
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      m_toolEntries.clear();
    }

    //----------------------------------------------------------------------------
    void ToolSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      // Update the transform repository with the latest registration
      float4x4 referenceToHMD(float4x4::identity());
      if (!m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD))
      {
        return;
      }

      if (!m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(referenceToHMD), true))
      {
        return;
      }

      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto entry : m_toolEntries)
      {
        entry->GetModelEntry()->SetVisible(true);
        entry->Update(timer);
      }
    }

    //----------------------------------------------------------------------------
    void ToolSystem::RegisterVoiceCallbacks(Sound::VoiceInputCallbackMap& callbackMap)
    {

    }

    //----------------------------------------------------------------------------
    task<void> ToolSystem::InitAsync(XmlDocument^ doc)
    {
      return create_task([this, doc]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention/Tools");
        if (doc->SelectNodes(xpath)->Length != 1)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"Invalid \"Tools\" tag in configuration.");
        }

        for (auto node : doc->SelectNodes(xpath))
        {
          if (!HasAttribute(L"IGTConnection", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool configuration does not contain \"IGTConnection\" attribute.");
          }
          Platform::String^ connectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
          m_connectionName = std::wstring(connectionName->Data());
          m_hashedConnectionName = HashString(connectionName);
        }

        xpath = ref new Platform::String(L"/HoloIntervention/Tools/Tool");
        if (doc->SelectNodes(xpath)->Length == 0)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"No tools defined in the configuration file. Check for the existence of Tools/Tool");
        }

        for (auto node : doc->SelectNodes(xpath))
        {
          // model, transform
          if (!HasAttribute(L"Model", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain model attribute.");
          }
          if (!HasAttribute(L"From", node) || !HasAttribute(L"To", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain transform attribute.");
          }
          Platform::String^ modelString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Model")->NodeValue);
          Platform::String^ fromString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
          Platform::String^ toString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
          if (modelString->IsEmpty() || fromString->IsEmpty() || toString->IsEmpty())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains an empty attribute.");
          }

          UWPOpenIGTLink::TransformName^ trName = ref new UWPOpenIGTLink::TransformName(fromString, toString);
          if (!trName->IsValid())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains invalid transform name.");
          }

          RegisterToolAsync(std::wstring(modelString->Data()), trName).then([this, node](uint64 token)
          {
            auto tool = GetTool(token);

            bool lerpEnabled;
            if (GetBooleanAttribute(L"LerpEnabled", node, lerpEnabled))
            {
              tool->GetModelEntry()->EnablePoseLerp(lerpEnabled);
            }

            float lerpRate;
            if (GetScalarAttribute<float>(L"LerpRate", node, lerpRate))
            {
              tool->GetModelEntry()->SetPoseLerpRate(lerpRate);
            }
          });
        }
      });
    }

  }
}