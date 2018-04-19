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

// Local includes
#include "pch.h"
#include "Common.h"
#include "TargetSphereTask.h"
#include "StepTimer.h"

// UI includes
#include "Icons.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"
#include "ToolEntry.h"
#include "ToolSystem.h"

// Rendering includes
#include "ModelRenderer.h"

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
      task<bool> TargetSphereTask::WriteConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention");
          if (document->SelectNodes(xpath)->Length != 1)
          {
            return false;
          }

          auto rootNode = document->SelectNodes(xpath)->Item(0);

          auto phantomElement = document->CreateElement("TargetSphereTask");
          phantomElement->SetAttribute(L"PhantomFrom", m_phantomToReferenceName->From());
          phantomElement->SetAttribute(L"PhantomTo", m_phantomToReferenceName->To());
          phantomElement->SetAttribute(L"StylusFrom", m_stylusTipToPhantomName->From());
          phantomElement->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));

          auto regionElement = document->CreateElement("Region");
          regionElement->SetAttribute("XMinMeters", m_boundsMeters[0].ToString());
          regionElement->SetAttribute("XMaxMeters", m_boundsMeters[1].ToString());
          regionElement->SetAttribute("YMinMeters", m_boundsMeters[2].ToString());
          regionElement->SetAttribute("YMaxMeters", m_boundsMeters[3].ToString());
          regionElement->SetAttribute("ZMinMeters", m_boundsMeters[4].ToString());
          regionElement->SetAttribute("ZMaxMeters", m_boundsMeters[5].ToString());

          phantomElement->AppendChild(regionElement);
          rootNode->AppendChild(phantomElement);

          return true;
        });
      }

      //----------------------------------------------------------------------------
      task<bool> TargetSphereTask::ReadConfigurationAsync(XmlDocument^ document)
      {
        return create_task([this, document]()
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/TargetSphereTask");
          if (document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          if (!m_transformRepository->ReadConfiguration(document))
          {
            return false;
          }

          // Connection and stylus details
          auto node = document->SelectNodes(xpath)->Item(0);

          if (!HasAttribute(L"IGTConnection", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"IGTConnection\" attributes. Cannot configure TargetSphereTask.");
            return false;
          }
          if (!HasAttribute(L"PhantomFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomFrom\" attribute. Cannot configure TargetSphereTask.");
            return false;
          }
          if (!HasAttribute(L"PhantomTo", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomTo\" attribute. Cannot configure TargetSphereTask.");
            return false;
          }
          if (!HasAttribute(L"StylusFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"StylusFrom\" attribute. Cannot configure TargetSphereTask.");
            return false;
          }

          if (HasAttribute(L"NumberOfPoints", node))
          {
            auto numPointsStr = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"NumberOfPoints")->NodeValue);
            if (!numPointsStr->IsEmpty())
            {
              std::wstringstream wss;
              wss << numPointsStr->Data();
              try
              {
                wss >> m_numberOfPoints;
              }
              catch (...) {}
            }
          }

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
              m_phantomToReferenceName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct PhantomTransformName from " + fromName + L" and " + toName + L" attributes. Cannot configure TargetSphereTask.");
              return false;
            }
          }

          fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"StylusFrom")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_stylusTipToPhantomName = ref new UWPOpenIGTLink::TransformName(fromName, m_phantomToReferenceName->From());
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct StylusTipTransformName from " + fromName + L" and " + m_phantomToReferenceName->From() + L" attributes. Cannot configure TargetSphereTask.");
              return false;
            }
          }

          // Position of targets
          xpath = ref new Platform::String(L"/HoloIntervention/TargetSphereTask/Region");
          if (document->SelectNodes(xpath)->Length == 0)
          {
            return false;
          }

          // Connection and model name details
          node = document->SelectNodes(xpath)->Item(0);

          std::map<int, std::wstring> vals =
          {
            { 0, L"XMinMeters" },
            { 1, L"XMaxMeters" },
            { 2, L"YMinMeters" },
            { 3, L"YMaxMeters" },
            { 4, L"ZMinMeters" },
            { 5, L"ZMaxMeters" }
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
              m_boundsMeters[pair.first] = std::stof(std::wstring(value->Data()));
            }
            catch (...)
            {
              WLOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to parse " + ref new Platform::String(pair.second.c_str()) + L" attribute in \"Region\" tag with value " + value + L". Cannot define task region bounds.");
              return false;
            }
          }

          // Validate bounds
          if (m_boundsMeters[1] < m_boundsMeters[0] || m_boundsMeters[3] < m_boundsMeters[2] || m_boundsMeters[5] < m_boundsMeters[4])
          {
            LOG_ERROR("Bounds are invalid. Cannot perform phantom task.");
            return false;
          }

          m_randomGenerator = std::mt19937(m_randomDevice());
          m_xDistribution = std::uniform_real_distribution<float>(m_boundsMeters[0], m_boundsMeters[1]);
          m_yDistribution = std::uniform_real_distribution<float>(m_boundsMeters[2], m_boundsMeters[3]);
          m_zDistribution = std::uniform_real_distribution<float>(m_boundsMeters[4], m_boundsMeters[5]);

          m_componentReady = true;
          return true;
        });
      }

      //----------------------------------------------------------------------------
      float3 TargetSphereTask::GetStabilizedPosition(SpatialPointerPose^ pose) const
      {
        if (m_targetModel != nullptr)
        {
          return float3(m_targetModel->GetCurrentPose().m41, m_targetModel->GetCurrentPose().m42, m_targetModel->GetCurrentPose().m43);
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 TargetSphereTask::GetStabilizedVelocity() const
      {
        if (m_targetModel != nullptr)
        {
          return m_targetModel->GetVelocity();
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float TargetSphereTask::GetStabilizePriority() const
      {
        return m_taskStarted && m_targetModel != nullptr && m_targetModel->IsInFrustum() ? PRIORITY_MODEL_TASK : PRIORITY_NOT_ACTIVE;
      }

      //----------------------------------------------------------------------------
      TargetSphereTask::TargetSphereTask(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, ToolSystem& toolSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer, UI::Icons& icons)
        : m_notificationSystem(notificationSystem)
        , m_networkSystem(networkSystem)
        , m_registrationSystem(registrationSystem)
        , m_toolSystem(toolSystem)
        , m_modelRenderer(modelRenderer)
        , m_icons(icons)
      {
        // 3 mm diameter
        m_modelRenderer.AddPrimitiveAsync(Rendering::PrimitiveType_SPHERE, 0.03f).then([this](uint64 primId)
        {
          m_targetModel = m_modelRenderer.GetModel(primId);
          m_targetModel->SetColour(DEFAULT_TARGET_COLOUR);
        });

        create_task([this]()
        {
          bool result = wait_until_condition([this]()
          {
            return m_toolSystem.GetToolByUserId(L"Stylus") != nullptr;
          }, 5000);

          if (result)
          {
            return m_toolSystem.GetToolByUserId(L"Stylus");
          }
          else
          {
            return std::shared_ptr<Tools::ToolEntry>();
          }
        }).then([this](std::shared_ptr<Tools::ToolEntry> entry)
        {
          if (entry == nullptr)
          {
            LOG_ERROR("Unable to locate stylus tool. Cannot create UI icon.");
            // Use a cylinder as a substitute
          }
          else
          {

          }
        });
      }

      //----------------------------------------------------------------------------
      TargetSphereTask::~TargetSphereTask()
      {
      }


      //----------------------------------------------------------------------------
      void TargetSphereTask::StopTask()
      {
        m_targetModel->SetVisible(false);
        m_taskStarted = false;
      }

      //-----------------------------------------------------------------------------
      void TargetSphereTask::GenerateNextRandomPoint()
      {
        m_targetPosition = float3(m_xDistribution(m_randomGenerator), m_yDistribution(m_randomGenerator), m_zDistribution(m_randomGenerator));
        m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Sphere", m_phantomToReferenceName->From()), transpose(make_float4x4_translation(m_targetPosition)), true);
      }

      //----------------------------------------------------------------------------
      void TargetSphereTask::Update(SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& timer)
      {
        if (!m_componentReady)
        {
          return;
        }

        if (m_networkSystem.IsConnected(m_hashedConnectionName))
        {
          m_trackedFrame = m_networkSystem.GetTrackedFrame(m_hashedConnectionName, m_latestTimestamp);
          if (m_trackedFrame == nullptr)
          {
            auto transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_phantomToReferenceName, m_latestTimestamp);
            if (transform == nullptr || !m_transformRepository->SetTransform(transform->Name, transform->Matrix, transform->Valid))
            {
              m_phantomWasValid = false;
              m_targetModel->SetColour(DISABLE_TARGET_COLOUR);
              return;
            }
          }
          else
          {
            if (!m_transformRepository->SetTransforms(m_trackedFrame))
            {
              m_phantomWasValid = false;
              m_targetModel->SetColour(DISABLE_TARGET_COLOUR);
              return;
            }
          }

          float4x4 registration;
          if (m_registrationSystem.GetReferenceToCoordinateSystemTransformation(coordinateSystem, registration))
          {
            m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HoloLens"), transpose(registration), true);
            auto result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Sphere", L"HoloLens"));

            // Update phantom model rendering
            if (!result->Key && m_phantomWasValid)
            {
              m_phantomWasValid = false;
              m_targetModel->SetColour(DISABLE_TARGET_COLOUR);
            }
            else if (result->Key && !m_phantomWasValid)
            {
              m_phantomWasValid = true;
              m_targetModel->SetColour(DEFAULT_TARGET_COLOUR);
            }

            // Update target pose
            m_targetModel->SetDesiredPose(transpose(result->Value));

            if (m_recordPointOnUpdate)
            {
              // Record a data point
              auto stylusTipPose = m_transformRepository->GetTransform(m_stylusTipToPhantomName);
              if (stylusTipPose->Key)
              {
                m_notificationSystem.QueueMessage(L"Point recorded. Next one created...");
                {
                  std::stringstream ss;
                  ss << "Point: " << stylusTipPose->Value.m41 << " " << stylusTipPose->Value.m42 << " " << stylusTipPose->Value.m43 << std::endl;
                  LOG_INFO(ss.str());
                }
                {
                  std::stringstream ss;
                  ss << "GroundTruth: " << m_targetPosition.x << " " << m_targetPosition.y << " " << m_targetPosition.z << std::endl;
                  LOG_INFO(ss.str());
                }
                {
                  std::stringstream ss;
                  ss << "Distance: " << distance(float3(stylusTipPose->Value.m41, stylusTipPose->Value.m42, stylusTipPose->Value.m43), m_targetPosition);
                  LOG_INFO(ss.str());
                }

                m_recordPointOnUpdate = false;

                m_pointsCollected++;
                if (m_pointsCollected >= m_numberOfPoints && m_numberOfPoints != 0)
                {
                  // Task is finished
                  m_notificationSystem.QueueMessage(L"Task finished!");
                  StopTask();
                }
                else
                {
                  // Generate new one within bounds
                  GenerateNextRandomPoint();
                }
              }
            }
          }
          else
          {
            m_phantomWasValid = false;
            m_targetModel->SetColour(DISABLE_TARGET_COLOUR);
          }
        }
        else
        {
          m_phantomWasValid = false;
          m_targetModel->SetColour(DISABLE_TARGET_COLOUR);
        }
      }

      //----------------------------------------------------------------------------
      void TargetSphereTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {
        callbackMap[L"start target task"] = [this](SpeechRecognitionResult ^ result)
        {
          if (m_taskStarted)
          {
            m_notificationSystem.QueueMessage(L"Task already running.");
            return;
          }

          GenerateNextRandomPoint();

          m_notificationSystem.QueueMessage(L"Sphere target task running.");
          m_targetModel->SetVisible(true);
          m_taskStarted = true;
        };

        callbackMap[L"stop target task"] = [this](SpeechRecognitionResult ^ result)
        {
          StopTask();
        };

        callbackMap[L"target point"] = [this](SpeechRecognitionResult ^ result)
        {
          if (!m_taskStarted)
          {
            m_notificationSystem.QueueMessage(L"Task not running.");
            return;
          }

          // If existing point, record
          m_recordPointOnUpdate = true;
        };
      }
    }
  }
}