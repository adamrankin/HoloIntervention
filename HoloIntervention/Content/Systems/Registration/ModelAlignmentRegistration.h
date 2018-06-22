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

#pragma once

// Local includes
#include "Common.h"
#include "IRegistrationMethod.h"
#include "IVoiceInput.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  class Debug;

  namespace Rendering
  {
    class ModelRenderer;
    class Model;
  }

  namespace Input
  {
    class SpatialInput;
  }

  namespace UI
  {
    class Icon;
    class Icons;
  }

  namespace Algorithm
  {
    class PointToLineRegistration;
  }

  namespace System
  {
    class NotificationSystem;
    class NetworkSystem;

    class ModelAlignmentRegistration : public IRegistrationMethod
    {
      // First = head pose, second = tracker pose
      typedef Windows::Foundation::Numerics::float4x4 Pose;
      typedef std::vector<Pose> PoseList;
      typedef Windows::Foundation::Numerics::float3 Position;
      typedef std::vector<Position> PositionList;

      enum Eye
      {
        EYE_LEFT,
        EYE_RIGHT
      };

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

      virtual void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ headPose, Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToHMDBox, Windows::Graphics::Holographic::HolographicCameraPose^ cameraPose);

    public:
      ModelAlignmentRegistration(HoloInterventionCore& core,
                                 System::NotificationSystem& notificationSystem,
                                 System::NetworkSystem& networkSystem,
                                 Rendering::ModelRenderer& modelRenderer,
                                 Input::SpatialInput& spatialInput,
                                 UI::Icons& icons,
                                 Debug& debug,
                                 DX::StepTimer& timer);
      ~ModelAlignmentRegistration();

    protected:
      // Cached references
      System::NotificationSystem&                           m_notificationSystem;
      System::NetworkSystem&                                m_networkSystem;
      Rendering::ModelRenderer&                             m_modelRenderer;
      UI::Icons&                                            m_icons;
      Input::SpatialInput&                                  m_spatialInput;
      Debug&                                                m_debug;
      DX::StepTimer&                                        m_timer;

      // State variables
      std::wstring                                          m_connectionName;
      uint64                                                m_hashedConnectionName;
      double                                                m_latestSphereTimestamp = 0.0;
      UWPOpenIGTLink::TransformName^                        m_sphereToReferenceTransformName = ref new UWPOpenIGTLink::TransformName(L"Sphere", L"Reference");
      std::atomic_bool                                      m_started = false;
      std::atomic_bool                                      m_calculating = false;
      std::shared_ptr<UI::Icon>                             m_sphereIconEntry = nullptr;

      // Input variables
      uint64                                                m_sourceObserverId = INVALID_TOKEN;

      // Behaviour variables
      std::atomic_bool                                      m_pointCaptureRequested = false;
      Eye                                                   m_currentEye = EYE_LEFT;

      // Registration data
      std::mutex                                            m_registrationAccessMutex;
      uint32                                                m_numberOfPointsToCollectPerEye;
      Position                                              m_previousSpherePosition_Ref = Position::zero();
      std::shared_ptr<Algorithm::PointToLineRegistration>   m_pointToLineRegistration;
      float                                                 m_registrationError = 0.f;

      // Stored data for back calculation of HMDtoHoloLens
      std::vector<Windows::Foundation::Numerics::float4x4>  m_sphereToReferenceTransforms;
      std::vector<Windows::Foundation::Numerics::float4x4>  m_eyeToHMDTransforms;
      std::vector<Windows::Foundation::Numerics::float4x4>  m_HMDToAnchorTransforms;

      // Model visualization
      Rendering::PrimitiveType                              m_primitiveType = Rendering::PrimitiveType_NONE;
      Windows::Foundation::Numerics::float4                 m_colour = Windows::Foundation::Numerics::float4::one();
      Windows::Foundation::Numerics::float3                 m_argument = Windows::Foundation::Numerics::float3::zero();
      size_t                                                m_tessellation = 16;
      std::atomic_bool                                      m_invertN = false;
      std::atomic_bool                                      m_rhCoords = true;
      std::shared_ptr<Rendering::Model>                     m_modelEntry = nullptr;
      uint64                                                m_trackingVisibleMessageId = INVALID_TOKEN;
      float                                                 m_invalidTrackingTimer = 0.f;

      // Constants
      static const float                                    MIN_DISTANCE_BETWEEN_POINTS_METER; // (currently 10mm)
      static const uint32                                   DEFAULT_NUMBER_OF_POINTS_TO_COLLECT = 12;
      static const float                                    INVALID_TRACKING_TIMEOUT_SEC;
      static const float                                    HOLOLENS_ICON_PITCH_RAD;
      static const float                                    HOLOLENS_ICON_YAW_RAD;
      static const float                                    HOLOLENS_ICON_ROLL_RAD;
    };
  }
}