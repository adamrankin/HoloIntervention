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

// Algorithms includes
#include "LandmarkRegistration.h"

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
          preopElement->SetAttribute(L"PhantomFrom", m_preopToReferenceName->From());
          preopElement->SetAttribute(L"PhantomTo", m_preopToReferenceName->To());
          preopElement->SetAttribute(L"StylusFrom", m_stylusTipTransformName->From());
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
          if (!HasAttribute(L"PhantomFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomFrom\" attribute. Cannot configure TouchingSphereTask.");
            return false;
          }
          if (!HasAttribute(L"PhantomTo", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"PhantomTo\" attribute. Cannot configure TouchingSphereTask.");
            return false;
          }
          if (!HasAttribute(L"StylusFrom", node))
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to locate \"StylusFrom\" attribute. Cannot configure TouchingSphereTask.");
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

          auto fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"PhantomFrom")->NodeValue);
          auto toName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"PhantomTo")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_preopToReferenceName = ref new UWPOpenIGTLink::TransformName(fromName, toName);
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct PhantomTransformName from " + fromName + L" and " + toName + L" attributes. Cannot configure TouchingSphereTask.");
              return false;
            }
          }

          fromName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"StylusFrom")->NodeValue);
          if (!fromName->IsEmpty() && !toName->IsEmpty())
          {
            try
            {
              m_stylusTipTransformName = ref new UWPOpenIGTLink::TransformName(fromName, m_preopToReferenceName->From());
            }
            catch (Platform::Exception^)
            {
              LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to construct StylusTipTransformName from " + fromName + L" and " + m_preopToReferenceName->From() + L" attributes. Cannot configure TouchingSphereTask.");
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
        if (m_componentReady && m_modelEntry != nullptr)
        {
          return float3(m_modelEntry->GetCurrentPose().m41, m_modelEntry->GetCurrentPose().m42, m_modelEntry->GetCurrentPose().m43);
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 PreOpImageTask::GetStabilizedVelocity() const
      {
        if (m_componentReady && m_modelEntry != nullptr)
        {
          return m_modelEntry->GetVelocity();
        }
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float PreOpImageTask::GetStabilizePriority() const
      {
        return (m_taskStarted && m_modelEntry != nullptr) ? PRIORITY_PHANTOM_TASK : PRIORITY_NOT_ACTIVE;
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
        if (!m_componentReady || !m_taskStarted)
        {
          return;
        }

        if (m_networkSystem.IsConnected(m_hashedConnectionName))
        {
          m_trackedFrame = m_networkSystem.GetTrackedFrame(m_hashedConnectionName, m_latestTimestamp);
          if (m_trackedFrame == nullptr || !m_transformRepository->SetTransforms(m_trackedFrame))
          {
            m_transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_preopToReferenceName, m_latestTimestamp);
            if (m_transform == nullptr || !m_transformRepository->SetTransform(m_preopToReferenceName, m_transform->Matrix, m_transform->Valid))
            {
              return;
            }
          }

          float4x4 registration;
          if (m_registrationSystem.GetReferenceToCoordinateSystemTransformation(coordinateSystem, registration))
          {
            m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HoloLens"), registration, true);
          }

          auto result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"HeartModel", L"HoloLens"));

          if (result->Key)
          {
            m_modelEntry->SetDesiredPose(result->Value);
          }
        }
      }

      //----------------------------------------------------------------------------
      void PreOpImageTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {
        callbackMap[L"start pre op"] = [this](SpeechRecognitionResult ^ result)
        {
          m_modelRenderer.AddModelAsync(L"heart.cmo").then([this](uint64 modelId)
          {
            m_modelEntry = m_modelRenderer.GetModel(modelId);
          });
          /*
          // TODO : get this up and running
          // for demo, load from file
          if (m_modelEntry != nullptr)
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
                m_commandId = cmdInfo.CommandId;
                create_task([this]()
                {
                  while (true)
                  {
                    auto poly = m_networkSystem.GetPolydata(m_hashedConnectionName, ref new Platform::String(m_modelName.c_str()));
                    if (poly == nullptr)
                    {
                      std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    else
                    {
                      // Parse polydata into a model
                      m_modelRenderer.AddModelAsync(m_polydata).then([this](uint64 modelId)
                      {
                        m_modelEntry = m_modelRenderer.GetModel(modelId);
                      });
                      break;
                    }
                  }
                });
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
          */
        };

        callbackMap[L"record point"] = [this](SpeechRecognitionResult ^ result)
        {
          auto pair = m_transformRepository->GetTransform(m_stylusTipTransformName);
          if (pair->Key)
          {
            m_points.push_back(float3(pair->Value.m41, pair->Value.m42, pair->Value.m43));
          }

          // TODO : for now, hardcoded, in the future, dynamic from command result sent back
          if (m_points.size() == 6)
          {
            std::vector<float3> landmarks = { {57.5909f, 161.627f, -98.7764f},
              {7.68349f, 169.246f, -24.3985f},
              {29.3939f, 155.906f, 103.148f},
              {-22.046f, 155.464f, 98.6673f},
              {-25.1729f, 167.911f, -43.6009f},
              {18.3745f, 163.052f, -103.733f}
            };
            for (auto& landmark : landmarks)
            {
              landmark = landmark / 1000.0f; // from millimeters to meters
            }
            m_landmarkRegistration->SetSourceLandmarks(landmarks);
            m_landmarkRegistration->SetTargetLandmarks(m_points);
            m_landmarkRegistration->CalculateTransformationAsync().then([this](float4x4 result)
            {
              m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"HeartModel", m_preopToReferenceName->From()), result, true);
            });
          }
        };

        callbackMap[L"reset points"] = [this](SpeechRecognitionResult ^ result)
        {
          m_points.clear();
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