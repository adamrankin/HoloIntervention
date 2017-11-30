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

// STL includes
#include <vector>

// Local includes
#include "IConfigurable.h"
#include "IRegistrationMethod.h"
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

// WinRT includes
#include <ppltasks.h>

namespace DX
{
  class StepTimer;
  class CameraResources;
}

namespace HoloIntervention
{
  namespace Physics
  {
    class PhysicsAPI;
  }

  namespace Rendering
  {
    class ModelEntry;
    class ModelRenderer;
  }

  namespace Algorithm
  {
    class LandmarkRegistration;
  }

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

    class RegistrationSystem : public Input::IVoiceInput, public IStabilizedComponent, public IConfigurable
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      RegistrationSystem(NetworkSystem& networkSystem,
                         Physics::PhysicsAPI& physicsAPI,
                         NotificationSystem& notificationSystem,
                         Rendering::ModelRenderer& modelRenderer);
      ~RegistrationSystem();

      void Update(DX::StepTimer& timer,
                  Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem,
                  Windows::UI::Input::Spatial::SpatialPointerPose^ headPose,
                  DX::CameraResources& cameraResources);

      Concurrency::task<void> LoadAppStateAsync();
      bool IsCameraActive() const;
      virtual void RegisterVoiceCallbacks(HoloIntervention::Input::VoiceInputCallbackMap& callbacks);

      bool GetReferenceToCoordinateSystemTransformation(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, Windows::Foundation::Numerics::float4x4& outTransform);

      void OnRegistrationComplete(Windows::Foundation::Numerics::float4x4);

    protected:
      bool CheckRegistrationValidity(Windows::Foundation::Numerics::float4x4);

    protected:
      // Cached references
      NotificationSystem&                                             m_notificationSystem;
      NetworkSystem&                                                  m_networkSystem;
      Rendering::ModelRenderer&                                       m_modelRenderer;
      Physics::PhysicsAPI&                                            m_physicsAPI;
      Windows::Data::Xml::Dom::XmlDocument^                           m_configDocument = nullptr;

      // State variables
      std::atomic_bool                                                m_forcePose = false;

      // Anchor variables
      std::atomic_bool                                                m_regAnchorRequested = false;
      uint64_t                                                        m_regAnchorModelId = 0;
      std::shared_ptr<Rendering::ModelEntry>                          m_regAnchorModel = nullptr;
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