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
#include "CameraResources.h"
#include "Math.h"
#include "ModelAlignmentRegistration.h"
#include "PointToLineRegistration.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"

// Intellisense include
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    // If this distance not achieved between points, ask the user to move the marker further
    const float ModelAlignmentRegistration::MIN_DISTANCE_BETWEEN_POINTS_METER = 0.1f;

    //----------------------------------------------------------------------------
    ModelAlignmentRegistration::ModelAlignmentRegistration(System::NotificationSystem& notificationSystem, System::NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_modelRenderer(modelRenderer)
      , m_numberOfPointsToCollectPerEye(DEFAULT_NUMBER_OF_POINTS_TO_COLLECT)
      , m_pointToLineRegistration(std::make_shared<Algorithm::PointToLineRegistration>())
    {

    }

    //----------------------------------------------------------------------------
    ModelAlignmentRegistration::~ModelAlignmentRegistration()
    {
    }

    //----------------------------------------------------------------------------
    float3 ModelAlignmentRegistration::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      auto modelPose = m_modelEntry->GetCurrentPose();
      return float3(modelPose.m41, modelPose.m42, modelPose.m43);
    }

    //----------------------------------------------------------------------------
    float3 ModelAlignmentRegistration::GetStabilizedVelocity() const
    {
      return m_modelEntry->GetVelocity();
    }

    //----------------------------------------------------------------------------
    float ModelAlignmentRegistration::GetStabilizePriority() const
    {
      return IsStarted() && m_modelEntry != nullptr ? PRIORITY_MODELALIGNMENT : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    task<bool> ModelAlignmentRegistration::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention");
        if (document->SelectNodes(xpath)->Length != 1)
        {
          return false;
        }

        auto rootNode = document->SelectNodes(xpath)->Item(0);

        auto elem = document->CreateElement("ModelAlignmentRegistration");
        elem->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));
        elem->SetAttribute(L"From", m_sphereToReferenceTransformName->From());
        elem->SetAttribute(L"To", m_sphereToReferenceTransformName->To());
        elem->SetAttribute(L"NumberOfPointsToCollectPerEye", m_numberOfPointsToCollectPerEye.ToString());
        elem->SetAttribute(L"Primitive", ref new Platform::String(Rendering::ModelRenderer::PrimitiveToString(m_primitiveType).c_str()));
        elem->SetAttribute(L"Argument1", m_argument.x.ToString());
        elem->SetAttribute(L"Argument2", m_argument.y.ToString());
        elem->SetAttribute(L"Argument3", m_argument.z.ToString());
        elem->SetAttribute(L"Tessellation", m_tessellation ? L"True" : L"False");
        elem->SetAttribute(L"RightHandedCoords", m_rhCoords ? L"True" : L"False");
        elem->SetAttribute(L"InvertN", m_invertN ? L"True" : L"False");

        rootNode->AppendChild(elem);

        return true;
      });
    }
#include <opencv2/core.hpp>

    //----------------------------------------------------------------------------
    task<bool> ModelAlignmentRegistration::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        Platform::String^ xpath = L"/HoloIntervention/ModelAlignmentRegistration";
        if (document->SelectNodes(xpath)->Length != 1)
        {
          // No configuration found, use defaults
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"No model alignment registration configuration found. Cannot use without key information.");
          return task_from_result(false);
        }

        auto node = document->SelectNodes(xpath)->Item(0);

        if (!GetAttribute(L"IGTConnection", node, m_connectionName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"Network attribute not defined for model alignment registration. Aborting.");
          return task_from_result(false);
        }

        m_hashedConnectionName = HashString(m_connectionName);

        std::wstring sphereCenterCoordFrameName;
        std::wstring referenceCoordinateFrameName;
        if (!GetAttribute(L"From", node, sphereCenterCoordFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"From coordinate system name attribute not defined for pivot calibrated phantom. Aborting.");
          return task_from_result(false);
        }
        if (!GetAttribute(L"To", node, referenceCoordinateFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"To cooordinate system name attribute not defined for pivot calibrated phantom. Aborting.");
          return task_from_result(false);
        }
        m_sphereToReferenceTransformName = ref new UWPOpenIGTLink::TransformName(
                                             ref new Platform::String(sphereCenterCoordFrameName.c_str()),
                                             ref new Platform::String(referenceCoordinateFrameName.c_str())
                                           );

        if (!GetScalarAttribute<uint32>(L"NumberOfPointsToCollectPerEye", node, m_numberOfPointsToCollectPerEye))
        {
          LOG(LogLevelType::LOG_LEVEL_WARNING, L"Buffer size not defined for optical registration. Defaulting to " + DEFAULT_NUMBER_OF_POINTS_TO_COLLECT.ToString());
        }

        Rendering::PrimitiveType type = Rendering::PrimitiveType_SPHERE;
        if (!HasAttribute(L"Primitive", node))
        {
          LOG_WARNING("Primitive type not defined. Defaulting to sphere.");
        }
        else
        {
          auto primTypeString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Primitive")->NodeValue);
          type = Rendering::ModelRenderer::StringToPrimitive(primTypeString);
        }

        Platform::String^ argument1String = nullptr;
        Platform::String^ argument2String = nullptr;
        Platform::String^ argument3String = nullptr;
        Platform::String^ tessellationString = nullptr;
        Platform::String^ rhcoordsString = nullptr;
        Platform::String^ invertnString = nullptr;

        if (HasAttribute(L"Argument1", node))
        {
          argument1String = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Argument1")->NodeValue);
        }
        if (HasAttribute(L"Argument2", node))
        {
          argument2String = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Argument2")->NodeValue);
        }
        if (HasAttribute(L"Argument3", node))
        {
          argument3String = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Argument3")->NodeValue);
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

        if (argument1String != nullptr && !argument1String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument1String->Data();
          wss >> m_argument.x;
        }
        if (argument2String != nullptr && !argument2String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument2String->Data();
          wss >> m_argument.y;
        }
        if (argument3String != nullptr && !argument3String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument3String->Data();
          wss >> m_argument.z;
        }
        if (tessellationString != nullptr && !tessellationString->IsEmpty())
        {
          std::wstringstream wss;
          wss << tessellationString->Data();
          wss >> m_tessellation;
        }
        if (rhcoordsString != nullptr && !rhcoordsString->IsEmpty())
        {
          m_rhCoords = IsEqualInsensitive(rhcoordsString, L"true");
        }
        if (invertnString != nullptr && !invertnString->IsEmpty())
        {
          m_invertN = IsEqualInsensitive(invertnString, L"true");
        }

        return m_modelRenderer.AddPrimitiveAsync(m_primitiveType, m_argument, m_tessellation, m_rhCoords, m_invertN).then([this](task<uint64> loadTask)
        {
          uint64 modelId = INVALID_TOKEN;
          try
          {
            modelId = loadTask.get();
          }
          catch (...)
          {
            LOG_ERROR("Unable to load primitive for model alignment registration.");
            return false;
          }

          m_modelEntry = m_modelRenderer.GetModel(modelId);
          if (m_modelEntry == nullptr)
          {
            LOG_ERROR("Unable to load primitive for model alignment registration.");
            return false;
          }

          m_componentReady = true;
          return true;
        });
      });
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::SetWorldAnchor(SpatialAnchor^ worldAnchor)
    {
      m_worldAnchor = worldAnchor;
      ResetRegistration();
    }

    //----------------------------------------------------------------------------
    Concurrency::task<bool> ModelAlignmentRegistration::StartAsync()
    {
      return create_task([this]()
      {
        if (!m_componentReady || m_worldAnchor == nullptr)
        {
          return false;
        }
        if (m_started)
        {
          auto remainingPoints = m_numberOfPointsToCollectPerEye - m_pointToLineRegistration->Count();
          m_notificationSystem.QueueMessage(L"Already running. Please capture " + remainingPoints.ToString() + L" more point" + (remainingPoints > 1 ? L"s" : L"") + L".");
          return true;
        }

        m_started = true;
        ResetRegistration();
        m_notificationSystem.QueueMessage(L"Please use only your LEFT eye to align the real and virtual sphere centers.");
        return true;
      });
    }

    //----------------------------------------------------------------------------
    Concurrency::task<bool> ModelAlignmentRegistration::StopAsync()
    {
      return create_task([this]()
      {
        m_started = false;
        m_notificationSystem.QueueMessage(L"Registration stopped.");
        m_latestTimestamp = 0.0;

        return true;
      });
    }

    //----------------------------------------------------------------------------
    bool ModelAlignmentRegistration::IsStarted() const
    {
      return m_started;
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::ResetRegistration()
    {
      std::lock_guard<std::mutex> lock(m_registrationAccessMutex);
      m_pointToLineRegistration->Reset();
      m_latestTimestamp = 0.0;
      m_referenceToAnchor = float4x4::identity();
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::EnableVisualization(bool enabled)
    {
      m_modelEntry->SetVisible(enabled);
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbacks)
    {
      callbacks[L"capture"] = [this](SpeechRecognitionResult ^ result)
      {
        m_pointCaptureRequested = true;
      };
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<float4x4>^ anchorToHMDBox, DX::CameraResources& cameraResources)
    {
      if (!m_started || !m_componentReady || !m_networkSystem.IsConnected(m_hashedConnectionName))
      {
        return;
      }

      float4x4 hmdToAnchor;
      if (!invert(anchorToHMDBox->Value, &hmdToAnchor))
      {
        assert(false);
        return;
      }

      float4x4 eyeToHMD;
      switch (m_currentEye)
      {
      case EYE_LEFT:
        // vpBuffer->view[0] = left
        float4x4 leftEyeToHMD;
        XMStoreFloat4x4(&leftEyeToHMD, XMLoadFloat4x4(&cameraResources.GetLatestViewProjectionBuffer().view[0]));
        eyeToHMD = leftEyeToHMD;
        break;
      case EYE_RIGHT:
        // vpBuffer->view[1] = right
        float4x4 rightEyeToHMD;
        XMStoreFloat4x4(&rightEyeToHMD, XMLoadFloat4x4(&cameraResources.GetLatestViewProjectionBuffer().view[1]));
        eyeToHMD = rightEyeToHMD;
        break;
      }

      // Separately, update the virtual model to be 1m in front of the current eye
      auto point = transform(float3(0, 0, 1), eyeToHMD);
      m_modelEntry->SetDesiredPose(make_float4x4_translation(point));

      if (m_pointCaptureRequested)
      {
        // grab latest transform
        auto sphereToReferenceTransform = m_networkSystem.GetTransform(m_hashedConnectionName, m_sphereToReferenceTransformName, m_latestTimestamp);
        m_latestTimestamp = sphereToReferenceTransform->Timestamp;
        if (sphereToReferenceTransform == nullptr || !sphereToReferenceTransform->Valid)
        {
          return;
        }

        //----------------------------------------------------------------------------
        // Optical tracking collection
        Position spherePosition_Ref(sphereToReferenceTransform->Matrix.m41, sphereToReferenceTransform->Matrix.m42, sphereToReferenceTransform->Matrix.m43);
        if (m_previousSpherePosition_Ref != Position::zero())
        {
          // Analyze current point and previous point for reasons to reject
          if (distance(spherePosition_Ref, m_previousSpherePosition_Ref) <= MIN_DISTANCE_BETWEEN_POINTS_METER)
          {
            return;
          }
        }

        //----------------------------------------------------------------------------
        // HoloLens ray collection
        auto eyeToAnchor = eyeToHMD * hmdToAnchor;
        float3 eyeOrigin_Anchor(eyeToAnchor.m41, eyeToAnchor.m42, eyeToAnchor.m43);
        float3 eyeForwardRay_Anchor(eyeToAnchor.m31, eyeToAnchor.m32, eyeToAnchor.m33);

        std::lock_guard<std::mutex> lock(m_registrationAccessMutex);
        // Only add them as pairs!
        m_pointToLineRegistration->AddLine(Line(eyeOrigin_Anchor, eyeForwardRay_Anchor));
        m_pointToLineRegistration->AddPoint(spherePosition_Ref);
        m_previousSpherePosition_Ref = spherePosition_Ref;

        if (m_pointToLineRegistration->Count() == m_numberOfPointsToCollectPerEye)
        {
          m_notificationSystem.QueueMessage(L"Please use only your RIGHT eye to align the real and virtual sphere centers.");
          m_currentEye = EYE_RIGHT;
        }
        else if (m_pointToLineRegistration->Count() == (m_numberOfPointsToCollectPerEye * 2))
        {
          m_started = false;
          m_notificationSystem.QueueMessage(L"Collection finished. Processing...");
          m_currentEye = EYE_LEFT;
          float error;
          m_pointToLineRegistration->ComputeAsync(error).then([this, &error](float4x4 matrix)
          {
            m_referenceToAnchor = matrix;
            if (m_completeCallback != nullptr)
            {
              m_completeCallback(matrix);
            }
            m_notificationSystem.QueueMessage(L"Registration finished with an error of " + error.ToString() + L"mm.");
            LOG_INFO(L"Registration finished with an error of " + error.ToString() + L"mm.");
          });
        }

        m_pointCaptureRequested = false;
      }
    }
  }
}