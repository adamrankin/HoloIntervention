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
#include "Debug.h"
#include "MathCommon.h"
#include "ModelAlignmentRegistration.h"
#include "PointToLineRegistration.h"

// Core includes
#include "CameraResources.h"
#include "StepTimer.h"

// Rendering includes
#include "Model.h"
#include "ModelRenderer.h"

// Input includes
#include "SpatialInput.h"

// UI includes
#include "Icons.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"

// Intellisense include
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    // If this distance not achieved between points, ask the user to move the marker further
    const float ModelAlignmentRegistration::MIN_DISTANCE_BETWEEN_POINTS_METER = 0.1f;
    const float ModelAlignmentRegistration::INVALID_TRACKING_TIMEOUT_SEC = 0.75;
    const float ModelAlignmentRegistration::HOLOLENS_ICON_PITCH_RAD = static_cast<float>(-M_PI_4 * 3 / 2);
    const float ModelAlignmentRegistration::HOLOLENS_ICON_YAW_RAD = static_cast<float>(-M_PI_4 * 2 / 3);
    const float ModelAlignmentRegistration::HOLOLENS_ICON_ROLL_RAD = 0.f;

    //----------------------------------------------------------------------------
    ModelAlignmentRegistration::ModelAlignmentRegistration(System::NotificationSystem& notificationSystem, System::NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer, Input::SpatialInput& spatialInput, UI::Icons& icons, Debug& debug, DX::StepTimer& timer)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_modelRenderer(modelRenderer)
      , m_icons(icons)
      , m_spatialInput(spatialInput)
      , m_debug(debug)
      , m_numberOfPointsToCollectPerEye(DEFAULT_NUMBER_OF_POINTS_TO_COLLECT)
      , m_pointToLineRegistration(std::make_shared<Algorithm::PointToLineRegistration>())
      , m_timer(timer)
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
        elem->SetAttribute(L"SphereFrom", m_sphereToReferenceTransformName->From());
        elem->SetAttribute(L"SphereTo", m_sphereToReferenceTransformName->To());
        elem->SetAttribute(L"NumberOfPointsToCollectPerEye", m_numberOfPointsToCollectPerEye.ToString());
        elem->SetAttribute(L"Primitive", ref new Platform::String(Rendering::ModelRenderer::PrimitiveToString(m_primitiveType).c_str()));
        elem->SetAttribute(L"Argument", m_argument.x.ToString() + L" " + m_argument.y.ToString() + L" " + m_argument.z.ToString());
        elem->SetAttribute(L"Colour", m_colour.x.ToString() + L" " + m_colour.y.ToString() + L" " + m_colour.z.ToString() + L" " + m_colour.w.ToString());
        elem->SetAttribute(L"Tessellation", m_tessellation ? L"True" : L"False");
        elem->SetAttribute(L"RightHandedCoords", m_rhCoords ? L"True" : L"False");
        elem->SetAttribute(L"InvertN", m_invertN ? L"True" : L"False");

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

        std::wstring fromName;
        std::wstring toName;
        if (!GetAttribute(L"SphereFrom", node, fromName))
        {
          LOG_WARNING(L"From coordinate system name attribute not defined for pivot calibrated phantom. Defaulting to \"Sphere\".");
        }
        if (!GetAttribute(L"SphereTo", node, toName))
        {
          LOG_WARNING(L"To cooordinate system name attribute not defined for pivot calibrated phantom. Defaulting to \"Reference\".");
        }
        m_sphereToReferenceTransformName = ref new UWPOpenIGTLink::TransformName(
                                             ref new Platform::String(fromName.c_str()),
                                             ref new Platform::String(toName.c_str())
                                           );

        if (!GetScalarAttribute<uint32>(L"NumberOfPointsToCollectPerEye", node, m_numberOfPointsToCollectPerEye))
        {
          LOG_WARNING(L"Buffer size not defined for optical registration. Defaulting to " + DEFAULT_NUMBER_OF_POINTS_TO_COLLECT.ToString());
        }

        m_primitiveType = Rendering::PrimitiveType_SPHERE;
        if (!HasAttribute(L"Primitive", node))
        {
          LOG_WARNING("Primitive type not defined. Defaulting to sphere.");
        }
        else
        {
          auto primTypeString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Primitive")->NodeValue);
          m_primitiveType = Rendering::ModelRenderer::StringToPrimitive(primTypeString);
        }

        Platform::String^ argumentString = nullptr;
        Platform::String^ colourString = nullptr;
        Platform::String^ tessellationString = nullptr;
        Platform::String^ rhcoordsString = nullptr;
        Platform::String^ invertnString = nullptr;

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

        if (argumentString != nullptr && !argumentString->IsEmpty())
        {
          std::wstringstream wss;
          wss << argumentString->Data();
          wss >> m_argument.x;
          wss >> m_argument.y;
          wss >> m_argument.z;
        }
        if (colourString != nullptr && !colourString->IsEmpty())
        {
          std::wstringstream wss;
          wss << colourString->Data();
          wss >> m_colour.x;
          wss >> m_colour.y;
          wss >> m_colour.z;
          wss >> m_colour.w;
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
          m_modelEntry->SetColour(m_colour);
          m_modelEntry->SetVisible(false);

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
    task<bool> ModelAlignmentRegistration::StartAsync()
    {
      return create_task([this]()
      {
        if (!m_componentReady || m_worldAnchor == nullptr)
        {
          return task_from_result(false);
        }
        if (m_started)
        {
          auto remainingPoints = m_numberOfPointsToCollectPerEye - m_pointToLineRegistration->Count();
          m_notificationSystem.QueueMessage(L"Already running. Please capture " + remainingPoints.ToString() + L" more point" + (remainingPoints > 1 ? L"s" : L"") + L".");
          return task_from_result(true);
        }

        return m_icons.AddEntryAsync(m_modelEntry, 0).then([this](std::shared_ptr<UI::Icon> entry)
        {
          m_sphereIconEntry = entry;
          m_sphereIconEntry->GetModel()->SetVisible(true);
          m_sphereIconEntry->GetModel()->SetOriginalColour(0.f, 0.9f, 0.f, 1.f);

          m_started = true;
          m_modelEntry->SetVisible(true);
          ResetRegistration();
          m_notificationSystem.QueueMessage(L"Please use only your LEFT eye to align the real and virtual sphere centers.", 4);

          return true;
        });
      });
    }

    //----------------------------------------------------------------------------
    Concurrency::task<bool> ModelAlignmentRegistration::StopAsync()
    {
      return create_task([this]()
      {
        m_icons.RemoveEntry(m_sphereIconEntry->GetId());
        m_sphereIconEntry = nullptr;

        m_currentEye = EYE_LEFT;
        m_modelEntry->SetVisible(false);
        m_started = false;
        m_notificationSystem.QueueMessage(L"Registration stopped.");
        m_latestSphereTimestamp = 0.0;

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
      m_latestSphereTimestamp = 0.0;
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

      callbacks[L"dump records"] = [this](SpeechRecognitionResult ^ result)
      {
        LOG_INFO("SphereToReferenceTransforms");
        for (auto& xForm : m_sphereToReferenceTransforms)
        {
          WLOG_INFO(PrintMatrix(xForm));
        }
        LOG_INFO("EyeToHMDTransforms");
        for (auto& xForm : m_eyeToHMDTransforms)
        {
          WLOG_INFO(PrintMatrix(xForm));
        }
        LOG_INFO("HMDToAnchorTransforms");
        for (auto& xForm : m_HMDToAnchorTransforms)
        {
          WLOG_INFO(PrintMatrix(xForm));
        }
      };
    }

    //----------------------------------------------------------------------------
    void ModelAlignmentRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<float4x4>^ anchorToHMDBox, HolographicCameraPose^ cameraPose)
    {
      if (!m_started || !m_componentReady || !m_networkSystem.IsConnected(m_hashedConnectionName) || m_modelEntry == nullptr)
      {
        return;
      }

      float4x4 hmdToAnchor;
      if (!invert(anchorToHMDBox->Value, &hmdToAnchor))
      {
        assert(false);
        return;
      }

      //       float4x4 anchorToEye;
      //       auto stereoTransform = cameraPose->TryGetViewTransform(m_worldAnchor->CoordinateSystem);
      //       if (stereoTransform == nullptr)
      //       {
      //         LOG_ERROR("Unable to request stereo view to anchor.");
      //         return;
      //       }
      //       XMStoreFloat4x4(&anchorToEye, XMLoadFloat4x4(m_currentEye == EYE_LEFT ? &stereoTransform->Value.Left : &stereoTransform->Value.Right));
      //       float4x4 eyeToAnchor;
      //       invert(anchorToEye, &eyeToAnchor);

      // Separately, update the virtual model to be 1m in front of the current eye
      float4x4 hmdToEye;
      auto stereoTransform = cameraPose->TryGetViewTransform(hmdCoordinateSystem);
      if (stereoTransform == nullptr)
      {
        LOG_ERROR("Unable to request stereo view to HMD.");
        return;
      }
      XMStoreFloat4x4(&hmdToEye, XMLoadFloat4x4(m_currentEye == EYE_LEFT ? &stereoTransform->Value.Left : &stereoTransform->Value.Right));

      float4x4 eyeToHMD;
      invert(hmdToEye, &eyeToHMD);
      auto point = transform(float3(0, 0, -1), eyeToHMD);
      m_modelEntry->SetDesiredPose(make_float4x4_translation(point));

      // Update icon logic
      m_notificationSystem.RemoveMessage(m_trackingVisibleMessageId);
      auto sphereToReferenceTransform = m_networkSystem.GetTransform(m_hashedConnectionName, m_sphereToReferenceTransformName, m_latestSphereTimestamp);
      if (sphereToReferenceTransform == nullptr || !sphereToReferenceTransform->Valid)
      {
        m_invalidTrackingTimer += static_cast<float>(m_timer.GetElapsedSeconds());

        if (m_invalidTrackingTimer > INVALID_TRACKING_TIMEOUT_SEC)
        {
          m_sphereIconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_GREYSCALE);

          m_notificationSystem.RemoveMessage(m_trackingVisibleMessageId);
          m_trackingVisibleMessageId = m_notificationSystem.QueueMessage(L"Can't see sphere!", 1.f);
          m_pointCaptureRequested = false;
        }

        return;
      }
      m_invalidTrackingTimer = 0.f;
      m_latestSphereTimestamp = sphereToReferenceTransform->Timestamp;
      m_sphereIconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_DEFAULT);

      if (m_pointCaptureRequested)
      {
        //----------------------------------------------------------------------------
        // Optical tracking collection
        Position spherePosition_Ref(sphereToReferenceTransform->Matrix.m14, sphereToReferenceTransform->Matrix.m24, sphereToReferenceTransform->Matrix.m34);
        if (m_previousSpherePosition_Ref != Position::zero())
        {
          // Analyze current point and previous point for reasons to reject
          if (distance(spherePosition_Ref, m_previousSpherePosition_Ref) <= MIN_DISTANCE_BETWEEN_POINTS_METER)
          {
            m_notificationSystem.QueueMessage(L"Please move the sphere further away from the previous point.");
            m_pointCaptureRequested = false;
            return;
          }
        }

        //----------------------------------------------------------------------------
        // HoloLens ray collection
        auto eyeToAnchor = eyeToHMD * hmdToAnchor;
        Point eyeOrigin_Anchor(eyeToAnchor.m41, eyeToAnchor.m42, eyeToAnchor.m43);
        Vector3 eyeForwardRay_Anchor(eyeToAnchor.m31, eyeToAnchor.m32, eyeToAnchor.m33);

        std::lock_guard<std::mutex> lock(m_registrationAccessMutex);
        // Only add them as pairs!
        m_pointToLineRegistration->AddLine(eyeOrigin_Anchor, eyeForwardRay_Anchor);
        m_pointToLineRegistration->AddPoint(spherePosition_Ref);

        m_previousSpherePosition_Ref = spherePosition_Ref;

        // Store entries for later calculation of HMDtoHoloLens
        m_sphereToReferenceTransforms.push_back(sphereToReferenceTransform->Matrix);
        m_eyeToHMDTransforms.push_back(eyeToHMD);
        m_HMDToAnchorTransforms.push_back(hmdToAnchor);
        //m_holoLensToReferenceTransforms.push_back(holoLensToReferenceTransform->Matrix);

        if (m_pointToLineRegistration->Count() == m_numberOfPointsToCollectPerEye)
        {
          m_notificationSystem.QueueMessage(L"Please use only your RIGHT eye to align the real and virtual sphere centers.", 8);
          m_currentEye = EYE_RIGHT;
        }
        else if (m_pointToLineRegistration->Count() == (m_numberOfPointsToCollectPerEye * 2))
        {
          this->StopAsync();
          m_notificationSystem.QueueMessage(L"Collection finished. Processing...");
          m_pointToLineRegistration->ComputeAsync(m_registrationError).then([this](float4x4 referenceToAnchor)
          {
            m_referenceToAnchor = referenceToAnchor;
            if (m_completeCallback != nullptr)
            {
              m_completeCallback(m_referenceToAnchor);
            }
            m_notificationSystem.QueueMessage(L"Registration finished with an error of " + m_registrationError.ToString() + L"mm.");
          });
        }
        else
        {
          if (m_pointToLineRegistration->Count() <= m_numberOfPointsToCollectPerEye)
          {
            m_notificationSystem.QueueMessage(L"Left eye captured: " + m_pointToLineRegistration->Count().ToString() + L"/" + m_numberOfPointsToCollectPerEye.ToString());
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Right eye captured: " + (m_pointToLineRegistration->Count() - m_numberOfPointsToCollectPerEye).ToString() + L"/" + m_numberOfPointsToCollectPerEye.ToString());
          }
        }

        m_pointCaptureRequested = false;
      }
    }
  }
}