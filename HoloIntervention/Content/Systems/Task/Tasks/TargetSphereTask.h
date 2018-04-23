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
#include "Configuration.h"
#include "IConfigurable.h"
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

// IGT includes
#include <IGTCommon.h>

// STL includes
#include <array>
#include <random>

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
    class Model;
  }

  namespace Input
  {
    class VoiceInput;
  }

  namespace UI
  {
    class Icons;
    class Icon;
  }

  namespace Network
  {
    class IGTConnector;
  }

  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;
    class ToolSystem;

    namespace Tasks
    {
      class TargetSphereTask : public IStabilizedComponent, public Input::IVoiceInput, public IConfigurable
      {
      public:
        virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
        virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

      public:
        virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
        virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
        virtual float GetStabilizePriority() const;

      public:
        virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);
        virtual void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& stepTimer);

        TargetSphereTask(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, ToolSystem& toolSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer, UI::Icons& icons);
        ~TargetSphereTask();

      protected:
        void StopTask();
        void GenerateNextRandomPoint();

      protected:
        // Cached system variables
        NotificationSystem&                                     m_notificationSystem;
        NetworkSystem&                                          m_networkSystem;
        ToolSystem&                                             m_toolSystem;
        RegistrationSystem&                                     m_registrationSystem;
        Rendering::ModelRenderer&                               m_modelRenderer;
        UI::Icons&                                              m_icons;

        std::wstring                                            m_connectionName = L"";
        uint64                                                  m_hashedConnectionName = 0;
        UWPOpenIGTLink::TransformName^                          m_phantomToReferenceName = ref new UWPOpenIGTLink::TransformName();
        double                                                  m_latestTimestamp = 0.0;

        // Target rendering variables
        std::shared_ptr<Rendering::Model>                       m_targetModel = nullptr;
        std::shared_ptr<Rendering::Model>                       m_cylinderModel = nullptr; // only used in case 'Stylus' tool can't be found
        std::shared_ptr<UI::Icon>                               m_stylusIcon = nullptr;
        Windows::Foundation::Numerics::float3                   m_targetPosition = Windows::Foundation::Numerics::float3::zero();
        std::array<float, 6>                                    m_boundsMeters = { 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
        const Windows::Foundation::Numerics::float3             DISABLE_TARGET_COLOUR = { 0.7f, 0.7f, 0.7f };
        const Windows::Foundation::Numerics::float3             DEFAULT_TARGET_COLOUR = { 0.f, 1.f, 0.f };
        const Windows::Foundation::Numerics::float3             HIGHLIGHT_TARGET_COLOUR = { 1.f, 0.f, 0.f };

        // Target task behaviour
        std::atomic_bool                                        m_taskStarted = false;
        std::atomic_bool                                        m_recordPointOnUpdate = false;
        uint32                                                  m_numberOfPoints = 10;
        uint32                                                  m_pointsCollected = 0;
        UWPOpenIGTLink::TrackedFrame^                           m_trackedFrame = ref new UWPOpenIGTLink::TrackedFrame();
        UWPOpenIGTLink::TransformName^                          m_stylusTipToPhantomName = ref new UWPOpenIGTLink::TransformName();
        UWPOpenIGTLink::TransformRepository^                    m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();

        // Random number generation
        std::random_device                                      m_randomDevice;
        std::mt19937                                            m_randomGenerator;
        std::uniform_real_distribution<float>                   m_xDistribution;
        std::uniform_real_distribution<float>                   m_yDistribution;
        std::uniform_real_distribution<float>                   m_zDistribution;

        static const float                                      TARGET_SPHERE_DIAMETER_MM;
        static const float                                      STYLUS_CYLINDER_ICON_HEIGHT_MM;
        static const float                                      STYLUS_CYLINDER_ICON_RADIUS_MM;
      };
    }
  }
}