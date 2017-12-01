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
#include "ModelAlignmentRegistration.h"
#include "LandmarkRegistration.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"

// Intellisense include
#include <WindowsNumerics.h>

using namespace Concurrency;
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
    {

    }

    //----------------------------------------------------------------------------
    ModelAlignmentRegistration::~ModelAlignmentRegistration()
    {
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
        elem->SetAttribute(L"From", m_pointToReferenceTransformName->From());
        elem->SetAttribute(L"To", m_pointToReferenceTransformName->To());
        rootNode->AppendChild(elem);

        return true;
      });
    }

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
        m_pointToReferenceTransformName = ref new UWPOpenIGTLink::TransformName(
                                            ref new Platform::String(sphereCenterCoordFrameName.c_str()),
                                            ref new Platform::String(referenceCoordinateFrameName.c_str())
                                          );

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

        size_t tessellation = 16;
        bool rhcoords = true;
        bool invertn = false;
        float3 argument = { 0.f, 0.f, 0.f };
        if (argument1String != nullptr && !argument1String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument1String->Data();
          wss >> argument.x;
        }
        if (argument2String != nullptr && !argument2String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument2String->Data();
          wss >> argument.y;
        }
        if (argument3String != nullptr && !argument3String->IsEmpty())
        {
          std::wstringstream wss;
          wss << argument3String->Data();
          wss >> argument.z;
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

        return m_modelRenderer.AddPrimitiveAsync(type, argument, tessellation, rhcoords, invertn).then([this](task<uint64> loadTask)
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
          m_notificationSystem.QueueMessage(L"Already running. Please capture " + (m_numberOfPointsToCollect - m_pointReferenceList.size()).ToString() + L" more points.");
          return true;
        }

        m_started = true;
        ResetRegistration();
        m_notificationSystem.QueueMessage(L"Capturing...");
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
      std::lock_guard<std::mutex> lock(m_pointAccessMutex);
      m_pointReferenceList.clear();
      m_latestTimestamp = 0.0;
      m_referenceToAnchor = float4x4::identity();
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::EnableVisualization(bool enabled)
    {
      m_modelEntry->SetVisible(enabled);
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<float4x4>^ anchorToHMDBox, DX::CameraResources& cameraResources)
    {
      if (!m_started || !m_componentReady)
      {
        return;
      }

      // grab latest transform
      auto transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_pointToReferenceTransformName, m_latestTimestamp);
      if (transform == nullptr || !transform->Valid)
      {
        return;
      }
      m_latestTimestamp = transform->Timestamp;

      float4x4 hmdToAnchor;
      if (!invert(anchorToHMDBox->Value, &hmdToAnchor))
      {
        // This had better be impossible!
        return;
      }
      Position newPointPosition(transform->Matrix.m41, transform->Matrix.m42, transform->Matrix.m43);

      if (m_previousPointPosition != Position::zero())
      {
        // Analyze current point and previous point for reasons to reject
        if (distance(newPointPosition, m_previousPointPosition) <= MIN_DISTANCE_BETWEEN_POINTS_METER)
        {
          return;
        }
      }

      std::lock_guard<std::mutex> lock(m_pointAccessMutex);
      m_pointReferenceList.push_back(newPointPosition);
      m_previousPointPosition = newPointPosition;
    }
  }
}