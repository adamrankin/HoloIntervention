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
#include "Common.h"
#include "IRegistrationMethod.h"
#include "IVoiceInput.h"

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
    class ModelEntry;
  }

  namespace System
  {
    class NotificationSystem;
    class NetworkSystem;
  }

  namespace System
  {
    class ModelAlignmentRegistration : public IRegistrationMethod
    {
      // First = head pose, second = tracker pose
      typedef Windows::Foundation::Numerics::float4x4 Pose;
      typedef std::vector<Pose> PoseList;
      typedef Windows::Foundation::Numerics::float3 Position;
      typedef std::vector<Position> PositionList;

    public:
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);

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
      ModelAlignmentRegistration(System::NotificationSystem& notificationSystem, System::NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer);
      ~ModelAlignmentRegistration();

    protected:
      // Cached references
      System::NotificationSystem&             m_notificationSystem;
      System::NetworkSystem&                  m_networkSystem;
      Rendering::ModelRenderer&               m_modelRenderer;

      // State variables
      std::wstring                            m_connectionName;
      uint64                                  m_hashedConnectionName;
      double                                  m_latestTimestamp = 0.0;
      UWPOpenIGTLink::TransformName^          m_pointToReferenceTransformName;
      std::atomic_bool                        m_started = false;
      std::atomic_bool                        m_calculating = false;

      // Behaviour variables
      std::atomic_bool                        m_pointCaptureRequested = false;

      // Point data
      std::mutex                              m_pointAccessMutex;
      uint32                                  m_numberOfPointsToCollect = 12;
      Position                                m_previousPointPosition = Position::zero();
      PositionList                            m_pointReferenceList;

      // Model visualization
      Rendering::PrimitiveType                m_primitiveType = Rendering::PrimitiveType_NONE;
      std::shared_ptr<Rendering::ModelEntry>  m_modelEntry = nullptr;

      // Constants
      static const float                      MIN_DISTANCE_BETWEEN_POINTS_METER; // (currently 10mm)
    };
  }
}