/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Common.h"
#include "MathCommon.h"
#include "Tool.h"
#include "ToolSystem.h"

// Rendering includes
#include "ModelRenderer.h"

// UI includes
#include "Icons.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
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
      std::shared_ptr<Tools::Tool> maxEntry(nullptr);
      float maxPriority(PRIORITY_NOT_ACTIVE);
      for (auto& entry : m_tools)
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
      std::shared_ptr<Tools::Tool> maxEntry(nullptr);
      float maxPriority(PRIORITY_NOT_ACTIVE);
      for (auto& entry : m_tools)
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
      for (auto& entry : m_tools)
      {
        if (entry->GetStabilizePriority() > maxPriority)
        {
          maxPriority = entry->GetStabilizePriority();
        }
      }

      return maxPriority;
    }

    //----------------------------------------------------------------------------
    task<bool> ToolSystem::WriteConfigurationAsync(XmlDocument^ document)
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
        toolsElem->SetAttribute(L"ShowIcons", m_showIcons ? L"true" : L"false");
        node->AppendChild(toolsElem);

        for (auto tool : m_tools)
        {
          auto toolElem = document->CreateElement("Tool");
          if (tool->GetModel()->IsPrimitive())
          {
            toolElem->SetAttribute(L"Primitive", ref new Platform::String(Rendering::ModelRenderer::PrimitiveToString(tool->GetModel()->GetPrimitiveType()).c_str()));

            toolElem->SetAttribute(L"Argument", tool->GetModel()->GetArgument().x.ToString() + L" " + tool->GetModel()->GetArgument().y.ToString() + L" " + tool->GetModel()->GetArgument().z.ToString());
            toolElem->SetAttribute(L"Colour", tool->GetModel()->GetCurrentColour().x.ToString() + L" " + tool->GetModel()->GetCurrentColour().y.ToString() + L" " + tool->GetModel()->GetCurrentColour().z.ToString() + L" " + tool->GetModel()->GetCurrentColour().w.ToString());
            toolElem->SetAttribute(L"Tessellation", tool->GetModel()->GetTessellation().ToString());
            toolElem->SetAttribute(L"RightHandedCoords", tool->GetModel()->GetRHCoords().ToString());
            toolElem->SetAttribute(L"InvertN", tool->GetModel()->GetInvertN().ToString());
          }
          else
          {
            toolElem->SetAttribute(L"Model", ref new Platform::String(tool->GetModel()->GetAssetLocation().c_str()));
          }
          
          toolElem->SetAttribute("ModelToObjectTransform", ref new Platform::String(PrintMatrix(tool->GetModelToObjectTransform()).c_str()));
          toolElem->SetAttribute(L"From", tool->GetCoordinateFrame()->From());
          toolElem->SetAttribute(L"To", tool->GetCoordinateFrame()->To());
          toolElem->SetAttribute(L"Id", ref new Platform::String(tool->GetUserId().c_str()));
          toolElem->SetAttribute(L"LerpEnabled", tool->GetModel()->GetLerpEnabled() ? L"true" : L"false");
          if (tool->GetModel()->GetLerpEnabled())
          {
            toolElem->SetAttribute(L"LerpRate", tool->GetModel()->GetLerpRate().ToString());
          }
          toolsElem->AppendChild(toolElem);
        }

        m_transformRepository->WriteConfiguration(document);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> ToolSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      if (!m_transformRepository->ReadConfiguration(document))
      {
        return task_from_result(false);
      }

      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention/Tools");
        if (document->SelectNodes(xpath)->Length != 1)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"Invalid \"Tools\" tag in configuration.");
        }

        auto node = document->SelectNodes(xpath)->Item(0);
        if (!HasAttribute(L"IGTConnection", node))
        {
          throw ref new Platform::Exception(E_FAIL, L"Tool configuration does not contain \"IGTConnection\" attribute.");
        }
        Platform::String^ connectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
        m_connectionName = std::wstring(connectionName->Data());
        m_hashedConnectionName = HashString(connectionName);

        if (HasAttribute(L"ShowIcons", node))
        {
          Platform::String^ showIcons = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ShowIcons")->NodeValue);
          m_showIcons = IsEqualInsensitive(showIcons, L"true");
        }

        xpath = ref new Platform::String(L"/HoloIntervention/Tools/Tool");
        if (document->SelectNodes(xpath)->Length == 0)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"No tools defined in the configuration file. Check for the existence of Tools/Tool");
        }

        for (auto node : document->SelectNodes(xpath))
        {
          Platform::String^ modelString = nullptr;
          Platform::String^ primString = nullptr;
          Platform::String^ fromString = nullptr;
          Platform::String^ toString = nullptr;
          Platform::String^ argumentString = nullptr;
          Platform::String^ colourString = nullptr;
          Platform::String^ tessellationString = nullptr;
          Platform::String^ rhcoordsString = nullptr;
          Platform::String^ invertnString = nullptr;
          Platform::String^ userId = nullptr;

          // model, transform
          if (HasAttribute(L"Id", node))
          {
            userId = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Id")->NodeValue);
          }
          else
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain Id attribute.");
          }
          if (!HasAttribute(L"Model", node) && !HasAttribute(L"Primitive", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain model or primitive attribute.");
          }
          if (!HasAttribute(L"From", node) || !HasAttribute(L"To", node))
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry does not contain transform attribute.");
          }
          if (HasAttribute(L"Model", node))
          {
            modelString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Model")->NodeValue);
          }
          if (HasAttribute(L"Primitive", node))
          {
            primString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Primitive")->NodeValue);
          }
          fromString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
          toString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
          if ((modelString != nullptr && modelString->IsEmpty()) || (primString != nullptr && primString->IsEmpty()) || fromString->IsEmpty() || toString->IsEmpty())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains an empty attribute.");
          }
          if (HasAttribute(L"Argument", node))
          {
            argumentString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Argument")->NodeValue);
          }
          if (HasAttribute(L"Colour", node))
          {
            colourString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Colour")->NodeValue);
          }
          if (HasAttribute(L"Tessellation", node))
          {
            tessellationString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Tessellation")->NodeValue);
          }
          if (HasAttribute(L"RightHandedCoords", node))
          {
            rhcoordsString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"RightHandedCoords")->NodeValue);
          }
          if (HasAttribute(L"InvertN", node))
          {
            invertnString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"InvertN")->NodeValue);
          }

          UWPOpenIGTLink::TransformName^ trName = ref new UWPOpenIGTLink::TransformName(fromString, toString);
          if (!trName->IsValid())
          {
            throw ref new Platform::Exception(E_FAIL, L"Tool entry contains invalid transform name.");
          }

          // Ensure unique tool id
          for (auto& tool : m_tools)
          {
            if (IsEqualInsensitive(userId, tool->GetUserId()))
            {
              LOG_ERROR("Duplicate tool ID used. Skipping tool configuration.");
              continue;
            }
          }

          float4x4 mat = float4x4::identity();
          if (HasAttribute(L"ModelToObjectTransform", node))
          {
            auto transformStr = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ModelToObjectTransform")->NodeValue);
            mat = ReadMatrix(transformStr);
          }

          task<uint64> registerTask;
          if (modelString != nullptr)
          {
            registerTask = RegisterToolAsync(std::wstring(modelString->Data()), userId, mat, false, trName);
          }
          else
          {
            size_t tessellation = 16;
            float4 colour(float4::one());
            bool rhcoords = true;
            bool invertn = false;
            float3 argument = { 0.f, 0.f, 0.f };
            if (argumentString != nullptr && !argumentString->IsEmpty())
            {
              std::wstringstream wss;
              wss << argumentString->Data();
              wss >> argument.x;
              wss >> argument.y;
              wss >> argument.z;
            }
            if (colourString != nullptr && !colourString->IsEmpty())
            {
              std::wstringstream wss;
              wss << colourString->Data();
              wss >> colour.x;
              wss >> colour.y;
              wss >> colour.z;
              wss >> colour.w;
            }
            if (tessellationString != nullptr && !tessellationString->IsEmpty())
            {
              std::wstringstream wss;
              wss << tessellationString->Data();
              wss >> tessellation;
            }
            if (rhcoordsString != nullptr && !rhcoordsString->IsEmpty())
            {
              rhcoords = IsEqualInsensitive(rhcoordsString, L"true");
            }
            if (invertnString != nullptr && !invertnString->IsEmpty())
            {
              invertn = IsEqualInsensitive(invertnString, L"true");
            }
            registerTask = RegisterToolAsync(std::wstring(primString->Data()), userId, mat, true, trName, colour, argument, tessellation, rhcoords, invertn);
          }
          registerTask.then([this, node](uint64 token)
          {
            auto tool = GetTool(token);

            bool lerpEnabled;
            if (GetBooleanAttribute(L"LerpEnabled", node, lerpEnabled))
            {
              tool->GetModel()->EnablePoseLerp(lerpEnabled);
            }

            float lerpRate;
            if (GetScalarAttribute<float>(L"LerpRate", node, lerpRate))
            {
              tool->GetModel()->SetPoseLerpRate(lerpRate);
            }
          });
        }

        m_componentReady = true;
        return true;
      });
    }

    //----------------------------------------------------------------------------
    ToolSystem::ToolSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer, NetworkSystem& networkSystem, UI::Icons& icons)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_icons(icons)
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
      return m_tools.size();
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Tools::Tool> ToolSystem::GetTool(uint64 token) const
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto iter = m_tools.begin(); iter != m_tools.end(); ++iter)
      {
        if (token == (*iter)->GetId())
        {
          return *iter;
        }
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Tools::Tool> ToolSystem::GetToolByUserId(const std::wstring& userId) const
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto iter = m_tools.begin(); iter != m_tools.end(); ++iter)
      {
        if (IsEqualInsensitive(userId, (*iter)->GetUserId()))
        {
          return *iter;
        }
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Tools::Tool> ToolSystem::GetToolByUserId(Platform::String^ userId) const
    {
      return GetToolByUserId(std::wstring(userId->Data()));
    }

    //----------------------------------------------------------------------------
    std::vector<std::shared_ptr<Tools::Tool>> ToolSystem::GetTools()
    {
      return m_tools;
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
    task<uint64> ToolSystem::RegisterToolAsync(const std::wstring& modelName, Platform::String^ userId, float4x4 modelToObjectTransform, const bool isPrimitive, UWPOpenIGTLink::TransformName^ coordinateFrame, float4 colour, float3 argument, size_t tessellation, bool rhcoords, bool invertn)
    {
      task<uint64> modelTask;
      if (!isPrimitive)
      {
        modelTask = m_modelRenderer.AddModelAsync(modelName);
      }
      else
      {
        modelTask = m_modelRenderer.AddPrimitiveAsync(modelName, argument, tessellation, rhcoords, invertn);
      }
      return modelTask.then([this, coordinateFrame, colour, modelToObjectTransform, userId](uint64 modelEntryId)
      {
        std::shared_ptr<Tools::Tool> entry = std::make_shared<Tools::Tool>(m_modelRenderer, m_networkSystem, m_icons, m_hashedConnectionName, coordinateFrame, m_transformRepository, userId);
        entry->SetModelToObjectTransform(modelToObjectTransform);
        auto modelEntry = m_modelRenderer.GetModel(modelEntryId);
        return entry->SetModelAsync(modelEntry).then([this, entry, modelEntry, colour]()
        {
          modelEntry->SetVisible(false);
          modelEntry->SetColour(colour);
          std::lock_guard<std::mutex> guard(m_entriesMutex);
          m_tools.push_back(entry);

          entry->ShowIcon(m_showIcons);

          return entry->GetId();
        });
      });
    }

    //----------------------------------------------------------------------------
    void ToolSystem::UnregisterTool(uint64 toolToken)
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto iter = m_tools.begin(); iter != m_tools.end(); ++iter)
      {
        if (toolToken == (*iter)->GetId())
        {
          m_tools.erase(iter);
          return;
        }
      }
    }

    //----------------------------------------------------------------------------
    void ToolSystem::ClearTools()
    {
      std::lock_guard<std::mutex> guard(m_entriesMutex);
      m_tools.clear();
    }

    //----------------------------------------------------------------------------
    void ToolSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      // Update the transform repository with the latest registration
      float4x4 referenceToHMD(float4x4::identity());
      bool registrationAvailable = m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD);
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", HOLOLENS_COORDINATE_SYSTEM_PNAME), transpose(referenceToHMD), registrationAvailable);

      std::lock_guard<std::mutex> guard(m_entriesMutex);
      for (auto entry : m_tools)
      {
        entry->Update(timer);
      }
    }

    //----------------------------------------------------------------------------
    void ToolSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"hide tools"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_entriesMutex);
        for (auto entry : m_tools)
        {
          entry->SetHiddenOverride(true);
        }
      };

      callbackMap[L"show tools"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::mutex> guard(m_entriesMutex);
        for (auto entry : m_tools)
        {
          entry->SetHiddenOverride(false);
        }
      };
    }

    //----------------------------------------------------------------------------
    void ToolSystem::ShowIcons(bool show)
    {
      for (auto& entry : m_tools)
      {
        entry->ShowIcon(show);
      }
    }

  }
}
