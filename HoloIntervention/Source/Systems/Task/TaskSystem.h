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

// Valhalla includes
#include <Input\IVoiceInput.h>
#include <Interfaces\ISerializable.h>
#include <Interfaces\IStabilizedComponent.h>

namespace DX
{
  class StepTimer;
}

namespace Valhalla
{
  namespace Rendering
  {
    class ModelRenderer;
  }

  namespace Input
  {
    class VoiceInput;
  }

  namespace UI
  {
    class Icons;
  }
}

namespace HoloIntervention
{
  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;
    class ToolSystem;

    namespace Tasks
    {
      class TargetSphereTask;
      class RegisterModelTask;
    }

    class TaskSystem : public Valhalla::IStabilizedComponent, public Valhalla::Input::IVoiceInput, public Valhalla::ISerializable
    {
    public:
      virtual concurrency::task<bool> SaveAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> LoadAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual void RegisterVoiceCallbacks(Valhalla::Input::VoiceInputCallbackMap& callbackMap);

    public:
      TaskSystem(Valhalla::ValhallaCore& core, NotificationSystem&, NetworkSystem&, ToolSystem&, RegistrationSystem&, Valhalla::Rendering::ModelRenderer&, Valhalla::UI::Icons&);
      ~TaskSystem();

      void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& stepTimer);

    protected:
      // Cached system variables
      NotificationSystem&                         m_notificationSystem;
      NetworkSystem&                              m_networkSystem;
      ToolSystem&                                 m_toolSystem;
      RegistrationSystem&                         m_registrationSystem;
      Valhalla::Rendering::ModelRenderer&         m_modelRenderer;
      Valhalla::UI::Icons&                        m_icons;

      std::shared_ptr<Tasks::TargetSphereTask>  m_touchingSphereTask;
      std::shared_ptr<Tasks::RegisterModelTask>   m_regModelTask;
    };
  }
}