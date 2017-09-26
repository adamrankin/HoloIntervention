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
#include "PreOpImageTask.h"
#include "StepTimer.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// Rendering includes
#include "ModelRenderer.h"

// STL includes
#include <random>

// Intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    namespace Tasks
    {
      //----------------------------------------------------------------------------
      task<bool> PreOpImageTask::WriteConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention");
          if (document->SelectNodes(xpath)->Length != 1)
          {
            return false;
          }

          auto rootNode = document->SelectNodes(xpath)->Item(0);

          auto preopElement = document->CreateElement("PreOpImageTask");
          preopElement->SetAttribute(L"From", m_preopToReferenceName->From());
          preopElement->SetAttribute(L"To", m_preopToReferenceName->To());
          preopElement->SetAttribute(L"ModelName", ref new Platform::String(m_modelName.c_str()));
          preopElement->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));

          return true;
        });
      }

      //----------------------------------------------------------------------------
      task<bool> PreOpImageTask::ReadConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/PreOpImageTask");
          if (document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          if (!m_transformRepository->ReadConfiguration(document))
          {
            return false;
          }

          // Connection and model name details
          auto node = document->SelectNodes(xpath)->Item(0);

          if (!HasAttribute(L"IGTConnection", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"IGTConnection\" attributes. Cannot configure PreOpImageTask.");
            return false;
          }
          if (!HasAttribute(L"From", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"From\" attributes. Cannot configure PreOpImageTask.");
            return false;
          }
          if (!HasAttribute(L"To", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"To\" attributes. Cannot configure PreOpImageTask.");
            return false;
          }
          if (!HasAttribute(L"ModelName", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"ModelName\" attributes. Cannot configure PreOpImageTask.");
            return false;
          }

          auto igtConnection = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
          if (igtConnection->IsEmpty())
          {
            return false;
          }
          m_connectionName = std::wstring(igtConnection->Data());
          m_hashedConnectionName = HashString(igtConnection);

          auto fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
          auto toName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_preopToReferenceName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct PreOpToReferenceName from " + fromName + L" and " + toName + L" attributes. Cannot configure PreOpImageTask.");
              return false;
            }
          }

          m_componentReady = true;
          return true;
        });
      }

      //----------------------------------------------------------------------------
      float3 PreOpImageTask::GetStabilizedPosition(SpatialPointerPose^ pose) const
      {
        if (m_componentReady)
        {
          return float3(m_model->GetCurrentPose().m41, m_model->GetCurrentPose().m42, m_model->GetCurrentPose().m43);
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 PreOpImageTask::GetStabilizedVelocity() const
      {
        if (m_componentReady)
        {
          return m_model->GetVelocity();
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float PreOpImageTask::GetStabilizePriority() const
      {
        return m_componentReady ? PRIORITY_PHANTOM_TASK : PRIORITY_NOT_ACTIVE;
      }

      //----------------------------------------------------------------------------
      PreOpImageTask::PreOpImageTask(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer)
        : m_notificationSystem(notificationSystem)
        , m_networkSystem(networkSystem)
        , m_registrationSystem(registrationSystem)
        , m_modelRenderer(modelRenderer)
      {
      }

      //----------------------------------------------------------------------------
      PreOpImageTask::~PreOpImageTask()
      {
      }


      //----------------------------------------------------------------------------
      void PreOpImageTask::Update(SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& timer)
      {
        if (!m_componentReady)
        {
          return;
        }

        if (m_networkSystem.IsConnected(m_hashedConnectionName))
        {
          m_trackedFrame = m_networkSystem.GetTrackedFrame(m_hashedConnectionName, m_latestTimestamp);
          if (m_trackedFrame == nullptr || !m_transformRepository->SetTransforms(m_trackedFrame))
          {
            return;
          }

          float4x4 registration;
          if (m_registrationSystem.GetReferenceToCoordinateSystemTransformation(coordinateSystem, registration))
          {
            m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HoloLens"), registration, true);
            auto result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(m_preopToReferenceName->From(), L"HoloLens"));

          }
        }
      }

      //----------------------------------------------------------------------------
      void PreOpImageTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {
        callbackMap[L"start pre op"] = [this](SpeechRecognitionResult ^ result)
        {
          if (m_model != nullptr)
          {
            m_notificationSystem.QueueMessage(L"Starting phantom task.");
            m_taskStarted = true;
          }
          else
          {
            // Async load phantom model from IGTLink
            create_task([this]()
            {
              m_notificationSystem.QueueMessage(L"Loading phantom.");

              std::map<std::wstring, std::wstring> commandParameters;
              commandParameters[L"FileName"] = m_modelName;

              return m_networkSystem.SendCommandAsync(m_hashedConnectionName, L"GetPolydata", commandParameters).then([this](UWPOpenIGTLink::CommandData cmdInfo)
              {
                if (!cmdInfo.SentSuccessfully)
                {
                  return false;
                }
                return true;
              });
            }).then([this](bool result)
            {
              if (!result)
              {
                m_notificationSystem.QueueMessage(L"Unable to start phantom task. Check connection.");
                return;
              }

              m_notificationSystem.QueueMessage(L"Starting phantom task.");
              m_taskStarted = true;
            });
          }
        };

        callbackMap[L"stop pre op"] = [this](SpeechRecognitionResult ^ result)
        {
          // TODO: Calculate results
          if (m_taskStarted)
          {
            m_notificationSystem.QueueMessage(L"Phantom task stopped.");
            m_taskStarted = false;
          }
        };
      }
    }
  }
}