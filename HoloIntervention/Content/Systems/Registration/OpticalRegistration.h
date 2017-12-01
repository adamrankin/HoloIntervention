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

#pragma once

// Local includes
#include "IRegistrationMethod.h"

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
    class NetworkSystem;
  }

  namespace Algorithm
  {
    class LandmarkRegistration;
  }

  namespace System
  {
    class OpticalRegistration : public IRegistrationMethod
    {
      // First = head pose, second = tracker pose
      typedef Windows::Foundation::Numerics::float3 Position;
      typedef std::vector<Position> PositionList;

    public:
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap) {};

    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      virtual void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor);

      virtual Concurrency::task<bool> StartAsync();
      virtual Concurrency::task<bool> StopAsync();
      virtual bool IsStarted() const;
      virtual void ResetRegistration();

      virtual void EnableVisualization(bool enabled);
      virtual void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ headPose, Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToHMDBox, DX::CameraResources& cameraResources);

    public:
      OpticalRegistration(System::NotificationSystem& notificationSystem, System::NetworkSystem& networkSystem);
      ~OpticalRegistration();

    protected:
      // Cached references
      System::NotificationSystem&                       m_notificationSystem;
      System::NetworkSystem&                            m_networkSystem;

      // Landmark registration
      std::shared_ptr<Algorithm::LandmarkRegistration>  m_landmarkRegistration;

      // State variables
      std::wstring                                      m_connectionName;
      uint64                                            m_hashedConnectionName;
      double                                            m_latestTimestamp = 0.0;
      UWPOpenIGTLink::TransformName^                    m_holoLensToReferenceName;
      std::atomic_bool                                  m_started = false;
      std::atomic_bool                                  m_calculating = false;

      // Behavior variables
      uint32                                            m_poseListRecalcThresholdCount;
      uint32                                            m_currentNewPointCount = 0;

      // Point data
      std::mutex                                        m_pointAccessMutex;
      PositionList                                      m_opticalPositionList;
      PositionList                                      m_hololensInAnchorPositionList;
      Position                                          m_previousOpticalPosition = Position::zero();
      Position                                          m_previousHoloLensPosition = Position::zero();

      // Constants
      static const uint32                               DEFAULT_LIST_RECALC_THRESHOLD = 100;
      static const float                                MIN_DISTANCE_BETWEEN_POINTS_METER; // (5mm)
    };
  }
}