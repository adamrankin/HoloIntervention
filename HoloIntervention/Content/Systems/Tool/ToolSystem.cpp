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
    float3 ToolSystem::GetStabilizedPosition() const
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
        return maxEntry->GetStabilizedPosition();
      }

      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 ToolSystem::GetStabilizedNormal(SpatialPointerPose^ pose) const
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
        return maxEntry->GetStabilizedNormal(pose);
      }

      return float3(0.f, 1.f, 0.f);
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
    ToolSystem::ToolSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer, NetworkSystem& networkSystem, StorageFolder^ configStorageFolder)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_modelRenderer(modelRenderer)
      , m_transformRepository(ref new UWPOpenIGTLink::TransformRepository())
      , m_networkSystem(networkSystem)
    {
      try
      {
        InitializeTransformRepositoryAsync(L"configuration.xml", configStorageFolder, m_transformRepository);
      }
      catch (Platform::Exception^)
      {
        m_notificationSystem.QueueMessage("Unable to initialize tool system.");
        return;
      }

      try
      {
        LoadXmlDocumentAsync(L"configuration.xml", configStorageFolder).then([this](XmlDocument ^ doc)
        {
          return InitAsync(doc).then([this]()
          {
            m_componentReady = true;
          });
        });
      }
      catch (Platform::Exception^)
      {
        m_notificationSystem.QueueMessage("Unable to initialize tool system.");
      }
    }

    //----------------------------------------------------------------------------
    ToolSystem::~ToolSystem()
    {
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Tools::ToolEntry> ToolSystem::GetTool(uint64 token)
    {
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
    uint64 ToolSystem::RegisterTool(const std::wstring& modelName, UWPOpenIGTLink::TransformName^ coordinateFrame)
    {
      std::shared_ptr<Tools::ToolEntry> entry = std::make_shared<Tools::ToolEntry>(m_modelRenderer, coordinateFrame, modelName, m_transformRepository);
      entry->GetModelEntry()->SetVisible(false);
      m_toolEntries.push_back(entry);
      return entry->GetId();
    }

    //----------------------------------------------------------------------------
    void ToolSystem::UnregisterTool(uint64 toolToken)
    {
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
      m_toolEntries.clear();
    }

    //----------------------------------------------------------------------------
    void ToolSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      UWPOpenIGTLink::TrackedFrame^ frame = m_networkSystem.GetTrackedFrame(m_hashedConnectionName, m_latestTimestamp);
      if (frame == nullptr)
      {
        return;
      }

      // Update the transform repository with the latest registration
      float4x4 referenceToHMD(float4x4::identity());
      if (!m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD))
      {
        return;
      }

      if (!m_transformRepository->SetTransforms(frame))
      {
        return;
      }

      if (!m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(referenceToHMD), true))
      {
        return;
      }

      for (auto entry : m_toolEntries)
      {
        entry->GetModelEntry()->SetVisible(true);
        entry->Update(timer);
      }
    }

    //----------------------------------------------------------------------------
    void ToolSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {

    }

    //----------------------------------------------------------------------------
    concurrency::task<void> ToolSystem::InitAsync(XmlDocument^ doc)
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
          if (!HasAttribute(L"ConnectionName", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool configuration does not contain \"ConnectionName\" attribute.");
          }
          Platform::String^ connectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ConnectionName")->NodeValue);
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
          if (!HasAttribute(L"Transform", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain transform attribute.");
          }
          Platform::String^ modelString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Model")->NodeValue);
          Platform::String^ transformString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Transform")->NodeValue);
          if (modelString->IsEmpty() || transformString->IsEmpty())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains an empty attribute.");
          }

          UWPOpenIGTLink::TransformName^ trName = ref new UWPOpenIGTLink::TransformName(transformString);
          if (!trName->IsValid())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains invalid transform name.");
          }

          auto token = RegisterTool(std::wstring(modelString->Data()), trName);
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
        }
      });
    }

  }
}