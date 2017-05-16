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

#pragma once

// Local includes
#include "IConfigurable.h"
#include "IRegistrationMethod.h"
#include "IStabilizedComponent.h"

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
    class NetworkSystem;

    class OpticalRegistration : public IRegistrationMethod
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      virtual void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor);

      virtual Concurrency::task<bool> StartAsync();
      virtual Concurrency::task<bool> StopAsync();
      virtual bool IsStarted();
      virtual void ResetRegistration();

      virtual void EnableVisualization(bool enabled);
      virtual void Update(Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToRequestedBox);

    public:
      OpticalRegistration(NotificationSystem& notificationSystem, NetworkSystem& networkSystem);
      ~OpticalRegistration();

    protected:
    };
  }
}