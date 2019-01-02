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

// STL includes
#include <vector>

// Local includes
#include "IRegistrationMethod.h"

// Valhalla includes
#include <Input\IVoiceInput.h>
#include <Interfaces\ILocatable.h>
#include <Interfaces\ISerializable.h>
#include <Interfaces\IStabilizedComponent.h>

// WinRT includes
#include <ppltasks.h>

namespace DX
{
  class StepTimer;
  class CameraResources;
}

namespace Valhalla
{
  class Debug;

  namespace Physics
  {
    class PhysicsAPI;
  }

  namespace UI
  {
    class Icons;
  }

  namespace Input
  {
    class SpatialInput;
  }

  namespace Rendering
  {
    class Model;
    class ModelRenderer;
  }

  namespace Algorithm
  {
    class LandmarkRegistration;
  }
}

namespace HoloIntervention
{
  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;

    enum RegistrationType
    {
      REGISTRATIONTYPE_NONE,
      REGISTRATIONTYPE_TOOLBASED,
      REGISTRATIONTYPE_OPTICAL,
      REGISTRATIONTYPE_CAMERA,
      REGISTRATIONTYPE_MODELALIGNMENT,


      REGISTRATIONTYPE_COUNT
    };

    class RegistrationSystem : public Valhalla::Input::IVoiceInput, public Valhalla::IStabilizedComponent, public Valhalla::ISerializable, public Valhalla::ILocatable
    {
    public:
      virtual void OnLocatabilityChanged(Windows::Perception::Spatial::SpatialLocatability locatability);

    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      virtual void RegisterVoiceCallbacks(Valhalla::Input::VoiceInputCallbackMap& callbacks);

    public:
      RegistrationSystem(Valhalla::ValhallaCore& core,
                         NetworkSystem& networkSystem,
                         Valhalla::Physics::PhysicsAPI& physicsAPI,
                         NotificationSystem& notificationSystem,
                         Valhalla::Rendering::ModelRenderer& modelRenderer,
                         Valhalla::Input::SpatialInput& spatialInput,
                         Valhalla::UI::Icons& icons,
                         Valhalla::Debug& debug,
                         DX::StepTimer& timer);
      ~RegistrationSystem();

      void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem,
                  Windows::UI::Input::Spatial::SpatialPointerPose^ headPose,
                  Windows::Graphics::Holographic::HolographicCameraPose^ cameraPose);

      Concurrency::task<bool> LoadAppStateAsync();
      bool IsCameraActive() const;

      bool GetReferenceToCoordinateSystemTransformation(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, Windows::Foundation::Numerics::float4x4& outTransform);

      void OnRegistrationComplete(Windows::Foundation::Numerics::float4x4);

    protected:
      bool CheckRegistrationValidity(Windows::Foundation::Numerics::float4x4);

    protected:
      // Cached references
      NotificationSystem&                                             m_notificationSystem;
      NetworkSystem&                                                  m_networkSystem;
      Valhalla::Rendering::ModelRenderer&                             m_modelRenderer;
      Valhalla::Physics::PhysicsAPI&                                  m_physicsAPI;
      Valhalla::UI::Icons&                                            m_icons;
      Valhalla::Debug&                                                m_debug;
      Valhalla::Input::SpatialInput&                                  m_spatialInput;
      Windows::Data::Xml::Dom::XmlDocument^                           m_configDocument = nullptr;
      DX::StepTimer&                                                  m_timer;

      // State variables
      std::atomic_bool                                                m_forcePose = false;
      Windows::Perception::Spatial::SpatialLocatability               m_locatability = Windows::Perception::Spatial::SpatialLocatability::Unavailable;
      std::atomic_bool                                                m_messageSent = false;

      // Anchor variables
      std::atomic_bool                                                m_regAnchorRequested = false;
      uint64_t                                                        m_regAnchorModelId = 0;
      std::shared_ptr<Valhalla::Rendering::Model>                     m_regAnchorModel = nullptr;
      Windows::Perception::Spatial::SpatialAnchor^                    m_regAnchor = nullptr;

      // Registration methods
      std::map<std::wstring, std::shared_ptr<IRegistrationMethod>>    m_knownRegistrationMethods;

      mutable std::mutex                                              m_registrationMethodMutex;
      std::shared_ptr<IRegistrationMethod>                            m_currentRegistrationMethod;
      Windows::Foundation::Numerics::float4x4                         m_cachedReferenceToAnchor = Windows::Foundation::Numerics::float4x4::identity();

      // Constants
      static Platform::String^                                        REGISTRATION_ANCHOR_NAME;
      static const std::wstring                                       REGISTRATION_ANCHOR_MODEL_FILENAME;

      static std::array<std::wstring, REGISTRATIONTYPE_COUNT>         REGISTRATION_TYPE_NAMES;
    };
  }
}