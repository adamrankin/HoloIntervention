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
#include "Configuration.h"
#include "IConfigurable.h"
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

// IGT includes
#include <IGTCommon.h>

// OS includes
#include <ppltasks.h>

// STL includes
#include <array>

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
    class PrimitiveEntry;
  }

  namespace Input
  {
    class VoiceInput;
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

    namespace Tasks
    {
      class PhantomTask : public IStabilizedComponent, public Input::IVoiceInput, public IConfigurable
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

        PhantomTask(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer);
        ~PhantomTask();

      protected:
        // Cached system variables
        NotificationSystem&                                     m_notificationSystem;
        NetworkSystem&                                          m_networkSystem;
        RegistrationSystem&                                     m_registrationSystem;
        Rendering::ModelRenderer&                               m_modelRenderer;

        std::wstring                                            m_modelName = L"";
        std::wstring                                            m_connectionName = L"";
        uint64                                                  m_hashedConnectionName = 0;
        UWPOpenIGTLink::TransformName^                          m_transformName = ref new UWPOpenIGTLink::TransformName();
        double                                                  m_latestTimestamp = 0.0;

        // Phantom rendering variables
        std::shared_ptr<Rendering::PrimitiveEntry>              m_targetModel = nullptr;
        Windows::Foundation::Numerics::float3                   m_targetPosition;
        std::atomic_bool                                        m_phantomWasValid = false;
        std::array<float, 6>                                    m_bounds = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
        double                                                  m_blinkTimer = 0.0;
        const double                                            PHANTOM_SPHERE_BLINK_TIME = 6.0;
        const Windows::Foundation::Numerics::float3             DEFAULT_TARGET_COLOUR = {0.f, 1.f, 0.f};
        const Windows::Foundation::Numerics::float3             HIGHLIGHT_TARGET_COLOUR = {1.f, 0.f, 0.f};

        // Phantom task behaviour
        std::atomic_bool                                        m_taskStarted = false;
        std::atomic_bool                                        m_recordPointOnUpdate = false;
        UWPOpenIGTLink::TrackedFrame^                           m_trackedFrame = ref new UWPOpenIGTLink::TrackedFrame();
        UWPOpenIGTLink::TransformName^                          m_stylusTipTransformName = ref new UWPOpenIGTLink::TransformName();
        UWPOpenIGTLink::TransformRepository^                    m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      };
    }
  }
}