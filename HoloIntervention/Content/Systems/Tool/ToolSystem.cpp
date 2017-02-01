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

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;

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
    float3 ToolSystem::GetStabilizedNormal() const
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
        return maxEntry->GetStabilizedNormal();
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
    ToolSystem::ToolSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_modelRenderer(modelRenderer)
      , m_transformRepository(ref new UWPOpenIGTLink::TransformRepository())
    {
      try
      {
        InitializeTransformRepositoryAsync(m_transformRepository, L"Assets\\Data\\configuration.xml");
      }
      catch (Platform::Exception^ e)
      {
        m_notificationSystem.QueueMessage(e->Message);
      }

      try
      {
        GetXmlDocumentFromFileAsync(L"Assets\\Data\\configuration.xml").then([this](XmlDocument ^ doc)
        {
          return InitAsync(doc).then([this]()
          {
            m_componentReady = true;
          });
        });
      }
      catch (Platform::Exception^ e)
      {
        m_notificationSystem.QueueMessage(e->Message);
      }
    }

    //----------------------------------------------------------------------------
    ToolSystem::~ToolSystem()
    {
    }

    //----------------------------------------------------------------------------
    uint64 ToolSystem::RegisterTool(const std::wstring& modelName, UWPOpenIGTLink::TransformName^ coordinateFrame)
    {
      std::shared_ptr<Tools::ToolEntry> entry = std::make_shared<Tools::ToolEntry>(m_modelRenderer, coordinateFrame, modelName, m_transformRepository);
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
    void ToolSystem::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      // Update the transform repository with the latest registration
      float4x4 trackerToRendering(float4x4::identity());
      try
      {
        trackerToRendering = m_registrationSystem.GetTrackerToCoordinateSystemTransformation(hmdCoordinateSystem);
      }
      catch (const std::exception&)
      {
        return;
      }

      m_transformRepository->SetTransforms(frame);
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(trackerToRendering), true);

      for (auto entry : m_toolEntries)
      {
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
      return create_task([ = ]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention/Tools/Tool");
        if (doc->SelectNodes(xpath)->Length == 0)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"No tools defined in the configuration file. Check for the existence of Tools/Tool");
        }

        for (auto toolNode : doc->SelectNodes(xpath))
        {
          // model, transform
          if (toolNode->Attributes->GetNamedItem(L"Model") == nullptr)
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain model attribute.");
          }
          if (toolNode->Attributes->GetNamedItem(L"Transform") == nullptr)
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain transform attribute.");
          }
          Platform::String^ modelString = dynamic_cast<Platform::String^>(toolNode->Attributes->GetNamedItem(L"Model")->NodeValue);
          Platform::String^ transformString = dynamic_cast<Platform::String^>(toolNode->Attributes->GetNamedItem(L"Transform")->NodeValue);
          if (modelString->IsEmpty() || transformString->IsEmpty())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains an empty attribute.");
          }

          UWPOpenIGTLink::TransformName^ trName = ref new UWPOpenIGTLink::TransformName(transformString);
          if (!trName->IsValid())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains invalid transform name.");
          }

          RegisterTool(std::wstring(modelString->Data()), trName);
        }
      });
    }

  }
}