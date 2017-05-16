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
#include "OpticalRegistration.h"
#include "LandmarkRegistration.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"

// Intellisense include
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    OpticalRegistration::OpticalRegistration(NotificationSystem& notificationSystem, NetworkSystem& networkSystem)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
    {

    }

    //----------------------------------------------------------------------------
    OpticalRegistration::~OpticalRegistration()
    {
    }

    //----------------------------------------------------------------------------
    float3 OpticalRegistration::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      return pose->Head->Position + (pose->Head->ForwardDirection * 2.f);
    }

    //----------------------------------------------------------------------------
    float3 OpticalRegistration::GetStabilizedNormal(SpatialPointerPose^ pose) const
    {
      return -pose->Head->ForwardDirection;
    }

    //----------------------------------------------------------------------------
    float3 OpticalRegistration::GetStabilizedVelocity() const
    {
      if (m_hololensInAnchorPositionList.size() <= 2)
      {
        return float3(0.f, 0.f, 0.f);
      }
      auto latestPosition = *(m_hololensInAnchorPositionList.rbegin());
      auto nextLatestPosition = *(++m_hololensInAnchorPositionList.rbegin());
      return latestPosition - nextLatestPosition;
    }

    //----------------------------------------------------------------------------
    float OpticalRegistration::GetStabilizePriority() const
    {
      return 0.5f;
    }

    //----------------------------------------------------------------------------
    task<bool> OpticalRegistration::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention");
        if (document->SelectNodes(xpath)->Length != 1)
        {
          return false;
        }

        m_transformRepository->WriteConfiguration(document);

        auto rootNode = document->SelectNodes(xpath)->Item(0);

        auto elem = document->CreateElement("OpticalRegistration");
        elem->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));
        elem->SetAttribute(L"BufferSize", m_poseListMinSize.ToString());
        elem->SetAttribute(L"OpticalHMDCoordinateFrame", m_opticalHMDToOpticalReferenceName->From());
        elem->SetAttribute(L"OpticalReferenceCoordinateFrame", m_opticalHMDToOpticalReferenceName->To());
        rootNode->AppendChild(elem);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> OpticalRegistration::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (!m_transformRepository->ReadConfiguration(document))
        {
          return false;
        }

        Platform::String^ xpath = L"/HoloIntervention/OpticalRegistration";
        if (document->SelectNodes(xpath)->Length != 1)
        {
          // No configuration found, use defaults
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"No optical registration configuration found. Cannot use without key information.");
          return false;
        }

        auto node = document->SelectNodes(xpath)->Item(0);

        if (!GetAttribute(L"IGTConnection", node, m_connectionName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"Network attribute not defined for optical registration. Aborting.");
          return false;
        }

        m_hashedConnectionName = HashString(m_connectionName);

        if (!GetScalarAttribute<uint32>(L"BufferSize", node, m_poseListMinSize))
        {
          LOG(LogLevelType::LOG_LEVEL_WARNING, L"Buffer size not defined for optical registration. Defaulting to " + DEFAULT_LIST_MAX_SIZE.ToString());
          m_poseListMinSize = DEFAULT_LIST_MAX_SIZE;
        }

        std::wstring hmdCoordinateFrameName;
        std::wstring referenceCoordinateFrameName;
        if (!GetAttribute(L"OpticalHMDCoordinateFrame", node, hmdCoordinateFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"OpticalHMDCoordinateFrame attribute not defined for optical registration. Aborting.");
          return false;
        }
        if (!GetAttribute(L"OpticalReferenceCoordinateFrame", node, referenceCoordinateFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"OpticalReferenceCoordinateFrame attribute not defined for optical registration. Aborting.");
          return false;
        }
        m_opticalHMDToOpticalReferenceName = ref new UWPOpenIGTLink::TransformName(
                                               ref new Platform::String(hmdCoordinateFrameName.c_str()),
                                               ref new Platform::String(referenceCoordinateFrameName.c_str())
                                             );

        m_componentReady = true;
        return true;
      });
    }

    //----------------------------------------------------------------------------
    void OpticalRegistration::SetWorldAnchor(SpatialAnchor^ worldAnchor)
    {
      ResetRegistration();
    }

    //----------------------------------------------------------------------------
    Concurrency::task<bool> OpticalRegistration::StartAsync()
    {
      if (!m_componentReady || m_worldAnchor == nullptr)
      {
        return task_from_result<bool>(false);
      }
      m_started = true;
      ResetRegistration();
      return task_from_result<bool>(true);
    }

    //----------------------------------------------------------------------------
    Concurrency::task<bool> OpticalRegistration::StopAsync()
    {
      m_started = false;
      return task_from_result<bool>(true);
    }

    //----------------------------------------------------------------------------
    bool OpticalRegistration::IsStarted()
    {
      return m_started;
    }

    //----------------------------------------------------------------------------
    void OpticalRegistration::ResetRegistration()
    {
      m_opticalPositionList.clear();
      m_hololensInAnchorPositionList.clear();
      m_latestTimestamp = 0.0;
      m_referenceToAnchor = float4x4::identity();
    }

    //----------------------------------------------------------------------------
    void OpticalRegistration::EnableVisualization(bool enabled)
    {
      // No visualization to this system!
    }

    //----------------------------------------------------------------------------
    void OpticalRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<float4x4>^ anchorToHMDBox)
    {
      if (!m_started || !m_componentReady)
      {
        return;
      }

      // grab latest network frame
      auto frame = m_networkSystem.GetTrackedFrame(m_hashedConnectionName, m_latestTimestamp);
      if (frame == nullptr)
      {
        auto transformFrame = m_networkSystem.GetTransformFrame(m_hashedConnectionName, m_latestTimestamp);
        if (transformFrame == nullptr)
        {
          return;
        }
        m_transformRepository->SetTransforms(transformFrame);
      }
      else
      {
        m_transformRepository->SetTransforms(frame);
      }

      auto opticalPose = float4x4::identity();
      if (!m_transformRepository->GetTransform(m_opticalHMDToOpticalReferenceName, &opticalPose))
      {
        return;
      }

      // grab latest head pose
      auto anchorToHMD = anchorToHMDBox->Value;
      if (!invert(anchorToHMD, &anchorToHMD))
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, L"Uninvertible transform sent as pose matrix. How is this possible?");
        return;
      }

      m_opticalPositionList.push_back(float3(opticalPose.m14, opticalPose.m24, opticalPose.m34)); // row-major form
      m_hololensInAnchorPositionList.push_back(transform(headPose->Head->Position, anchorToHMD));

      if (m_opticalPositionList.size() >= m_poseListMinSize && !m_calculating)
      {
        m_landmarkRegistration->SetSourceLandmarks(m_opticalPositionList);
        m_landmarkRegistration->SetTargetLandmarks(m_hololensInAnchorPositionList);

        m_calculating = true;
        m_landmarkRegistration->CalculateTransformationAsync().then([this](float4x4 result)
        {
          m_calculating = false;
          m_completeCallback(result);
        });
      }
      else
      {
        if (m_opticalPositionList.size() % 25 == 0)
        {
          m_notificationSystem.QueueMessage(m_opticalPositionList.size().ToString() + L" positions collected.");
        }
      }
    }
  }
}