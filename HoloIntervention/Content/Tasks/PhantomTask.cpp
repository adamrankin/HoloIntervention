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

#include "pch.h"
#include "Common.h"
#include "PhantomTask.h"

// System includes
#include "NetworkSystem.h"

// Rendering includes
#include "ModelEntry.h"

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    namespace Tasks
    {
      //----------------------------------------------------------------------------
      task<bool> PhantomTask::WriteConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention");
          if(document->SelectNodes(xpath)->Length != 1)
          {
            return false;
          }

          auto rootNode = document->SelectNodes(xpath)->Item(0);

          auto phantomElement = document->CreateElement("PhantomTask");
          phantomElement->SetAttribute(L"ModelName", ref new Platform::String(m_modelName.c_str()));
          phantomElement->SetAttribute(L"From", m_transformName->From());
          phantomElement->SetAttribute(L"To", m_transformName->To());
          phantomElement->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));

          auto targetsElement = document->CreateElement("Targets");
          for(auto& target : m_targets)
          {
            auto targetElement = document->CreateElement("Target");
            std::wstringstream ss;
            ss << target.x << " " << target.y << " " << target.z;
            targetElement->SetAttribute(L"Position", ref new Platform::String(ss.str().c_str()));
            targetsElement->AppendChild(targetElement);
          }

          phantomElement->AppendChild(targetsElement);
          rootNode->AppendChild(phantomElement);

          return true;
        });
      }

      //----------------------------------------------------------------------------
      task<bool> PhantomTask::ReadConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/PhantomTask");
          if(document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          // Connection and model name details
          auto node = document->SelectNodes(xpath)->Item(0);

          if(!HasAttribute(L"ModelName", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"ModelName\" attribute. Cannot configure PhantomTask.");
          }
          if(!HasAttribute(L"IGTConnection", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"IGTConnection\" attributes. Cannot configure PhantomTask.");
          }
          if(!HasAttribute(L"From", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"From\" attribute. Cannot configure PhantomTask.");
          }
          if(!HasAttribute(L"To", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"To\" attribute. Cannot configure PhantomTask.");
          }

          Platform::String^ modelName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ModelName")->NodeValue);
          if(modelName->IsEmpty())
          {
            return false;
          }
          m_modelName = std::wstring(modelName->Data());

          auto igtConnection = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
          if(igtConnection->IsEmpty())
          {
            return false;
          }
          m_connectionName = std::wstring(igtConnection->Data());
          m_hashedConnectionName = HashString(igtConnection);

          auto fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
          auto toName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
          if(!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_transformName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch(Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct TransformName from " + fromName + L" and " + toName + L" attributes. Cannot configure PhantomTask.");
              return false;
            }
          }

          // Position of targets
          xpath = ref new Platform::String(L"/HoloIntervention/PhantomTask/Targets/Target");
          if(document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          for(auto node : document->SelectNodes(xpath))
          {
            if(!HasAttribute(L"Position", node))
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, "Missing \"Position\" attribute in \"Target\" tag. Skipping.");
              continue;
            }

            auto position = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Position")->NodeValue);
            std::wstringstream wss;
            wss << position->Data();
            float x, y, z;
            try
            {
              wss >> x;
              wss >> y;
              wss >> z;
            }
            catch(...)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"\"Position=" + position + L"\" cannot be parsed. Skipping.");
              continue;
            }

            m_targets.push_back(float3(x, y, z));
          }
          m_componentReady = true;

          return true;
        });
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedPosition(SpatialPointerPose^ pose) const
      {
        if(m_phantomModel != nullptr)
        {
          auto& matrix = m_phantomModel->GetCurrentPose();
          return float3(matrix.m41, matrix.m42, matrix.m43);
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedVelocity() const
      {
        if(m_phantomModel != nullptr)
        {
          return m_phantomModel->GetVelocity();
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float PhantomTask::GetStabilizePriority() const
      {
        return m_componentReady ? PRIORITY_PHANTOM_TASK : PRIORITY_NOT_ACTIVE;
      }

      //----------------------------------------------------------------------------
      PhantomTask::PhantomTask(NetworkSystem& networkSystem)
        : m_networkSystem(networkSystem)
      {
      }

      //----------------------------------------------------------------------------
      PhantomTask::~PhantomTask()
      {
      }


      //----------------------------------------------------------------------------
      void PhantomTask::Update()
      {
        if(!m_componentReady)
        {
          return;
        }

        if(m_networkSystem.IsConnected(m_hashedConnectionName) && m_phantomModel == nullptr)
        {
          // Request phantom model

        }

        auto transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_transformName, m_latestTimestamp);
        if(transform != nullptr)
        {

        }
      }

      //----------------------------------------------------------------------------
      void PhantomTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {

      }
    }
  }
}