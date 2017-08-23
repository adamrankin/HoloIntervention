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
#include "PhantomTask.h"
#include "StepTimer.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// Rendering includes
#include "ModelRenderer.h"
#include "PrimitiveEntry.h"

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
      task<bool> PhantomTask::WriteConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention");
          if (document->SelectNodes(xpath)->Length != 1)
          {
            return false;
          }

          auto rootNode = document->SelectNodes(xpath)->Item(0);

          auto phantomElement = document->CreateElement("PhantomTask");
          phantomElement->SetAttribute(L"ModelName", ref new Platform::String(m_modelName.c_str()));
          phantomElement->SetAttribute(L"PhantomFrom", m_transformName->From());
          phantomElement->SetAttribute(L"PhantomTo", m_transformName->To());
          phantomElement->SetAttribute(L"StylusFrom", m_stylusTipTransformName->From());
          phantomElement->SetAttribute(L"StylusTo", m_stylusTipTransformName->To());
          phantomElement->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));

          auto regionElement = document->CreateElement("Region");
          regionElement->SetAttribute("XMin", m_bounds[0].ToString());
          regionElement->SetAttribute("XMax", m_bounds[1].ToString());
          regionElement->SetAttribute("YMin", m_bounds[2].ToString());
          regionElement->SetAttribute("YMax", m_bounds[3].ToString());
          regionElement->SetAttribute("ZMin", m_bounds[4].ToString());
          regionElement->SetAttribute("ZMax", m_bounds[5].ToString());

          phantomElement->AppendChild(regionElement);
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

          if (!HasAttribute(L"ModelName", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"ModelName\" attribute. Cannot configure PhantomTask.");
            return false;
          }
          if (!HasAttribute(L"IGTConnection", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"IGTConnection\" attributes. Cannot configure PhantomTask.");
            return false;
          }
          if (!HasAttribute(L"PhantomFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomFrom\" attribute. Cannot configure PhantomTask.");
            return false;
          }
          if (!HasAttribute(L"PhantomTo", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomTo\" attribute. Cannot configure PhantomTask.");
            return false;
          }
          if (!HasAttribute(L"StylusFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"StylusFrom\" attribute. Cannot configure PhantomTask.");
            return false;
          }
          if (!HasAttribute(L"StylusTo", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"StylusTo\" attribute. Cannot configure PhantomTask.");
            return false;
          }

          Platform::String^ modelName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ModelName")->NodeValue);
          if (modelName->IsEmpty())
          {
            return false;
          }
          m_modelName = std::wstring(modelName->Data());

          auto igtConnection = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
          if (igtConnection->IsEmpty())
          {
            return false;
          }
          m_connectionName = std::wstring(igtConnection->Data());
          m_hashedConnectionName = HashString(igtConnection);

          auto fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"PhantomFrom")->NodeValue);
          auto toName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"PhantomTo")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_transformName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct PhantomTransformName from " + fromName + L" and " + toName + L" attributes. Cannot configure PhantomTask.");
              return false;
            }
          }

          fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"StylusFrom")->NodeValue);
          toName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"StylusTo")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_stylusTipTransformName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct StylusTipTransformName from " + fromName + L" and " + toName + L" attributes. Cannot configure PhantomTask.");
              return false;
            }
          }

          // Position of targets
          xpath = ref new Platform::String(L"/HoloIntervention/PhantomTask/Region");
          if (document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          // Connection and model name details
          node = document->SelectNodes(xpath)->Item(0);

          std::map<int, std::wstring> vals =
          {
            {0, L"XMin"},
            {1, L"XMax"},
            {2, L"YMin"},
            {3, L"YMax"},
            {4, L"ZMin"},
            {5, L"ZMax"}
          };
          for (auto pair : vals)
          {
            if (!HasAttribute(pair.second, node))
            {
              WLOG(LogLevelType::LOG_LEVEL_ERROR, L"Missing " + pair.second + L" attribute in \"Region\" tag. Cannot define task region bounds.");
              return false;
            }
            auto value = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(ref new Platform::String(pair.second.c_str()))->NodeValue);
            try
            {
              m_bounds[pair.first] = std::stof(std::wstring(value->Data()));
            }
            catch (...)
            {
              WLOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to parse " + ref new Platform::String(pair.second.c_str()) + L" attribute in \"Region\" tag with value " + value + L". Cannot define task region bounds.");
              return false;
            }
          }

          // Validate bounds
          if (m_bounds[1] < m_bounds[0] || m_bounds[3] < m_bounds[2] || m_bounds[5] < m_bounds[4])
          {
            LOG_ERROR("Bounds are invalid. Cannot perform phantom task.");
            return false;
          }

          m_componentReady = true;
          return true;
        });
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedPosition(SpatialPointerPose^ pose) const
      {
        if (m_targetModel != nullptr)
        {
          return float3(m_targetModel->GetCurrentPose().m41, m_targetModel->GetCurrentPose().m42, m_targetModel->GetCurrentPose().m43);
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedVelocity() const
      {
        if (m_targetModel != nullptr)
        {
          return m_targetModel->GetVelocity();
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float PhantomTask::GetStabilizePriority() const
      {
        return m_componentReady ? PRIORITY_PHANTOM_TASK : PRIORITY_NOT_ACTIVE;
      }

      //----------------------------------------------------------------------------
      PhantomTask::PhantomTask(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer)
        : m_notificationSystem(notificationSystem)
        , m_networkSystem(networkSystem)
        , m_registrationSystem(registrationSystem)
        , m_modelRenderer(modelRenderer)
      {
        // 3 mm diameter
        auto primId = m_modelRenderer.AddPrimitive(Rendering::PrimitiveType_SPHERE, 0.03f);
        m_targetModel = m_modelRenderer.GetPrimitive(primId);
        m_targetModel->SetColour(DEFAULT_TARGET_COLOUR);
      }

      //----------------------------------------------------------------------------
      PhantomTask::~PhantomTask()
      {
      }


      //----------------------------------------------------------------------------
      void PhantomTask::Update(SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& timer)
      {
        if (!m_componentReady)
        {
          return;
        }

        // Update phantom blinking if needed
        if (m_targetModel && m_blinkTimer >= 0.0)
        {
          if (std::floor(m_blinkTimer - timer.GetElapsedSeconds()) != std::floor(m_blinkTimer))
          {
            m_targetModel->SetVisible(!m_targetModel->IsVisible());
          }

          m_blinkTimer -= timer.GetElapsedSeconds();
          if (m_blinkTimer <= 0.0)
          {
            m_blinkTimer = 0.0;
            m_targetModel->SetVisible(true);
            m_targetModel->SetColour(DEFAULT_TARGET_COLOUR);
          }
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
            auto result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(m_transformName->From(), L"HoloLens"));

            // Update phantom model rendering
            if (!result->Key && m_phantomWasValid)
            {
              m_phantomWasValid = false;
              m_targetModel->SetColour(float3(0.6f, 0.6f, 0.6f));
            }
            else if (result->Key && !m_phantomWasValid)
            {
              m_phantomWasValid = true;
              m_targetModel->SetColour(DEFAULT_TARGET_COLOUR);
            }

            // Update target pose
            auto pair = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Phantom", L"HoloLens"));
            m_targetModel->SetDesiredPose(pair->Value);

            if (m_recordPointOnUpdate)
            {
              // Record a data point
              auto result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(m_stylusTipTransformName->From(), L"Reference"));
              if (result->Key)
              {
                m_notificationSystem.QueueMessage(L"Point recorded. Next one created...");
                {
                  std::stringstream ss;
                  ss << "Point: " << result->Value.m41 << " " << result->Value.m42 << " " << result->Value.m43 << std::endl;
                  LOG_INFO(ss.str());
                }

                {
                  std::stringstream ss;
                  ss << "GroundTruth: " << m_targetPosition.x << " " << m_targetPosition.y << " " << m_targetPosition.z << std::endl;
                  LOG_INFO(ss.str());
                }
                m_recordPointOnUpdate = false;

                // Generate new one within bounds
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_real_distribution<float> dis(m_bounds[0], m_bounds[1]);
                auto x = dis(gen);

                std::uniform_real_distribution<float> ydis(m_bounds[2], m_bounds[3]);
                auto y = dis(gen);

                std::uniform_real_distribution<float> zdis(m_bounds[4], m_bounds[5]);
                m_targetPosition = float3(x, y, dis(gen));

                m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Phantom", L"Reference"), make_float4x4_translation(m_targetPosition), true);
              }
            }
          }
        }
      }

      //----------------------------------------------------------------------------
      void PhantomTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {
        callbackMap[L"start phantom task"] = [this](SpeechRecognitionResult ^ result)
        {
          m_notificationSystem.QueueMessage(L"Starting phantom task.");
          m_taskStarted = true;
        };

        callbackMap[L"stop phantom task"] = [this](SpeechRecognitionResult ^ result)
        {
          // Calculate results

          m_taskStarted = false;
        };

        callbackMap[L"record point"] = [this](SpeechRecognitionResult ^ result)
        {
          if (!m_taskStarted)
          {
            return;
          }

          // If existing point, record
          m_recordPointOnUpdate = true;
        };

        callbackMap[L"where's the point"] = [this](SpeechRecognitionResult ^ result)
        {
          if (!m_taskStarted)
          {
            return;
          }

          // Blink the point
          m_blinkTimer = PHANTOM_SPHERE_BLINK_TIME;
          m_targetModel->SetVisible(false);
          m_targetModel->SetColour(HIGHLIGHT_TARGET_COLOUR);
        };
      }
    }
  }
}