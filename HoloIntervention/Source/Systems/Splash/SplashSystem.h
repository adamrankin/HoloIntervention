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
#include <Common\Common.h>
#include <Interfaces\IStabilizedComponent.h>

namespace DX
{
  class StepTimer;
}

namespace Valhalla
{
  namespace Rendering
  {
    class Slice;
    class SliceRenderer;
  }
}

namespace HoloIntervention
{

  namespace System
  {
    class SplashSystem : public Valhalla::IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      SplashSystem(Valhalla::Rendering::SliceRenderer& sliceRenderer);
      ~SplashSystem();

      void StartSplash();
      void EndSplash();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

    protected:
      // Cached entries
      Valhalla::Rendering::SliceRenderer&                 m_sliceRenderer;

      uint64                                              m_sliceToken = Valhalla::INVALID_TOKEN;
      std::shared_ptr<Valhalla::Rendering::Slice>         m_sliceEntry = nullptr;
      float                                               m_welcomeTimerSec = 0.f;
      std::wstring                                        m_splashImageFilename = L"Assets\\Images\\HoloIntervention.png";

      static const float                                  LERP_RATE;
      static const float                                  MINIMUM_WELCOME_DISPLAY_TIME_SEC; // The splash screen doesn't say it's "ready" until this time has elapsed
    };
  }
}