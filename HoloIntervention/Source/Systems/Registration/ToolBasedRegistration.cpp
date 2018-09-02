update/*====================================================================
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
#include "CameraResources.h"
#include "ToolBasedRegistration.h"

// System includes
#include "NetworkSystem.h"

// STL includes
#include <sstream>

// Intellisense
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Platform;
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

    //----------------------------------------------------------------------------
    void ToolBasedRegistration::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"disable tool registration rotation"] = [this](SpeechRecognitionResult ^ result)
      {
        m_rotationEnabled = false;
      };

      callbackMap[L"enable tool registration rotation"] = [this](SpeechRecognitionResult ^ result)
      {
        m_rotationEnabled = true;
      };
    }

    //----------------------------------------------------------------------------
    float3 ToolBasedRegistration::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 ToolBasedRegistration::GetStabilizedVelocity() const
    {
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float ToolBasedRegistration::GetStabilizePriority() const
    {
      return PRIORITY_MANUAL;
    }

    //----------------------------------------------------------------------------
    task<bool> ToolBasedRegistration::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (document->SelectNodes(L"/HoloIntervention")->Length != 1)
        {
          return false;
        }

        auto rootNode = document->SelectNodes(L"/HoloIntervention")->Item(0);

        auto camRegElem = document->CreateElement(L"ToolBasedRegistration");
        camRegElem->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));
        camRegElem->SetAttribute(L"From", m_toolCoordinateFrameName->From());
        camRegElem->SetAttribute(L"To", m_toolCoordinateFrameName->To());
        rootNode->AppendChild(camRegElem);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> ToolBasedRegistration::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (!m_transformRepository->ReadConfiguration(document))
        {
          return false;
        }

        auto xpath = ref new Platform::String(L"/HoloIntervention/ToolBasedRegistration");
        if (document->SelectNodes(xpath)->Length == 0)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"No manual registration defined in the configuration file.");
        }

        auto node = document->SelectNodes(xpath)->Item(0);

        if (!HasAttribute(L"IGTConnection", node))
        {
          throw ref new Platform::Exception(E_FAIL, L"Manual registration entry does not contain \"IGTConnection\" attribute.");
        }
        Platform::String^ igtConnectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
        m_connectionName = std::wstring(igtConnectionName->Data());
        m_hashedConnectionName = HashString(igtConnectionName);

        std::wstring fromFrameName;
        std::wstring toFrameName;
        if (!GetAttribute(L"From", node, fromFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"FromFrameName attribute not defined for manual registration. Aborting.");
          return false;
        }
        if (!GetAttribute(L"To", node, toFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"ToFrameName attribute not defined for manual registration. Aborting.");
          return false;
        }
        m_toolCoordinateFrameName = ref new UWPOpenIGTLink::TransformName(
                                      ref new Platform::String(fromFrameName.c_str()),
                                      ref new Platform::String(toFrameName.c_str())
                                    );

        m_baselineNeeded = true;
        return true;
      });
    }

    //----------------------------------------------------------------------------
    void ToolBasedRegistration::SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor)
    {

    }

    //----------------------------------------------------------------------------
    ToolBasedRegistration::ToolBasedRegistration(HoloInterventionCore& core, System::NetworkSystem& networkSystem)
      : IRegistrationMethod(core)
      , m_networkSystem(networkSystem)
    {
    }

    //----------------------------------------------------------------------------
    ToolBasedRegistration::~ToolBasedRegistration()
    {
    }

    //----------------------------------------------------------------------------
    task<bool> ToolBasedRegistration::StartAsync()
    {
      m_baselineNeeded = true;
      m_started = true;
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    task<bool> ToolBasedRegistration::StopAsync()
    {
      m_started = false;
      m_registrationMatrix = m_accumulatorMatrix * m_registrationMatrix;
      m_accumulatorMatrix = float4x4::identity();
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    bool ToolBasedRegistration::IsStarted() const
    {
      return m_started;
    }

    //----------------------------------------------------------------------------
    void ToolBasedRegistration::ResetRegistration()
    {
      m_baselineNeeded = true;
      m_registrationMatrix = float4x4::identity();
      m_accumulatorMatrix = float4x4::identity();
    }

    //----------------------------------------------------------------------------
    void ToolBasedRegistration::EnableVisualization(bool enabled)
    {

    }

    //----------------------------------------------------------------------------
    void ToolBasedRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, IBox<float4x4>^ anchorToHMDBox, HolographicCameraPose^ cameraPose)
    {
      if (!m_started)
      {
        return;
      }

      // grab latest network frame
      auto transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_toolCoordinateFrameName, m_latestTimestamp);
      if (transform == nullptr || !transform->Valid)
      {
        return;
      }
      m_latestTimestamp = transform->Timestamp;

      auto opticalPose = transpose(transform->Matrix);

      if (!m_rotationEnabled)
      {
        opticalPose = make_float4x4_translation(opticalPose.m41, opticalPose.m42, opticalPose.m43);
      }

      if (m_baselineNeeded)
      {
        m_baselinePose = opticalPose;
        if (!invert(m_baselinePose, &m_baselineInverse))
        {
          m_baselineNeeded = true;
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to invert pose transformation. How is this possible?");
          return;
        }
        m_baselineNeeded = false;
        return;
      }

      if (!invert(opticalPose * m_baselineInverse, &m_accumulatorMatrix))
      {
        m_baselineNeeded = true;
      }
      else if (m_completeCallback)
      {
        m_completeCallback(m_accumulatorMatrix * m_registrationMatrix);
      }
    }

    //----------------------------------------------------------------------------
    float4x4 ToolBasedRegistration::GetRegistrationTransformation() const
    {
      return m_accumulatorMatrix * m_registrationMatrix;
    }
  }
}