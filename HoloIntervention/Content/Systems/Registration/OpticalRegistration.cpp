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
    const float OpticalRegistration::MIN_DISTANCE_BETWEEN_POINTS_METER = 0.001f;

    //----------------------------------------------------------------------------
    OpticalRegistration::OpticalRegistration(NotificationSystem& notificationSystem, NetworkSystem& networkSystem)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_landmarkRegistration(std::make_shared<LandmarkRegistration>())
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
        elem->SetAttribute(L"RecalcThresholdCount", m_poseListRecalcThresholdCount.ToString());
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

        if (!GetScalarAttribute<uint32>(L"RecalcThresholdCount", node, m_poseListRecalcThresholdCount))
        {
          LOG(LogLevelType::LOG_LEVEL_WARNING, L"Buffer size not defined for optical registration. Defaulting to " + DEFAULT_LIST_RECALC_THRESHOLD.ToString());
          m_poseListRecalcThresholdCount = DEFAULT_LIST_RECALC_THRESHOLD;
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
      m_worldAnchor = worldAnchor;
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

      // grab latest transform
      auto transform = m_networkSystem.GetTransform(m_hashedConnectionName, m_opticalHMDToOpticalReferenceName, m_latestTimestamp);
      if (transform == nullptr)
      {
        return;
      }
      m_latestTimestamp = transform->Timestamp;
      m_transformRepository->SetTransform(m_opticalHMDToOpticalReferenceName, transform->Matrix, transform->Valid);

      // Grab latest head pose
      auto HMDToAnchor = float4x4::identity();
      if (!invert(anchorToHMDBox->Value, &HMDToAnchor))
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, L"Uninvertible transform sent as pose matrix. How is this possible?");
        return;
      }

      Position newOpticalPosition(transform->Matrix.m14, transform->Matrix.m24, transform->Matrix.m34);
      Position newHoloLensPosition(HMDToAnchor.m41, HMDToAnchor.m42, HMDToAnchor.m43);

      if (m_previousOpticalPosition != Position::zero())
      {
        // Analyze current point and previous point for reasons to reject
        if (distance(newOpticalPosition, m_previousOpticalPosition) <= MIN_DISTANCE_BETWEEN_POINTS_METER ||
            distance(newHoloLensPosition, m_previousHoloLensPosition) <= MIN_DISTANCE_BETWEEN_POINTS_METER)
        {
          return;
        }
      }
      m_opticalPositionList.push_back(newOpticalPosition);
      m_previousOpticalPosition = newOpticalPosition;

      m_hololensInAnchorPositionList.push_back(newHoloLensPosition);
      m_previousHoloLensPosition = newHoloLensPosition;

      m_currentNewPointCount++;

      if (m_currentNewPointCount >= m_poseListRecalcThresholdCount && !m_calculating)
      {
        m_notificationSystem.QueueMessage(m_opticalPositionList.size().ToString() + L" positions collected.");
        m_currentNewPointCount = 0;
        m_landmarkRegistration->SetSourceLandmarks(m_opticalPositionList);
        m_landmarkRegistration->SetTargetLandmarks(m_hololensInAnchorPositionList);

        m_calculating = true;
        m_landmarkRegistration->CalculateTransformationAsync().then([this](task<float4x4> regTask)
        {
          try
          {
            float4x4 result = regTask.get();
            m_completeCallback(result);
          }
          catch (const std::exception&)
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to calculate registration.");
          }
          m_calculating = false;
        });
      }
    }
  }
}