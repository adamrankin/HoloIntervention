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
#include "IConfigurable.h"
#include "IStabilizedComponent.h"

// STL includes
#include <functional>

namespace HoloIntervention
{
  namespace System
  {
    class IRegistrationMethod : public IStabilizedComponent, public IConfigurable
    {
    public:
      virtual void RegisterTransformUpdatedCallback(std::function<void(Windows::Foundation::Numerics::float4x4)> function) { m_completeCallback = function; }
      virtual bool HasRegistration() const { return m_hasRegistration; }
      virtual Windows::Foundation::Numerics::float4x4 GetRegistrationTransformation() const { return m_referenceToAnchor; }

      virtual Windows::Perception::Spatial::SpatialAnchor^ GetWorldAnchor()
      {
        std::lock_guard<std::mutex> guard(m_anchorLock);
        return m_worldAnchor;
      };
      virtual void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor) = 0;

      virtual Concurrency::task<bool> StartAsync() = 0;
      virtual Concurrency::task<bool> StopAsync() = 0;
      virtual bool IsStarted() = 0;
      virtual void ResetRegistration() = 0;

      virtual void EnableVisualization(bool enabled) = 0;
      virtual void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ headPose, Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem, Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToHMDBox) = 0;

    protected:
      // Anchor resources
      std::mutex                                                   m_anchorLock;
      Windows::Perception::Spatial::SpatialAnchor^                 m_worldAnchor = nullptr;

      std::function<void(Windows::Foundation::Numerics::float4x4)> m_completeCallback;
      Windows::Foundation::Numerics::float4x4                      m_referenceToAnchor = Windows::Foundation::Numerics::float4x4::identity();
      std::atomic_bool                                             m_hasRegistration = false;
    };
  }
}