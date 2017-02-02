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
#include "IStabilizedComponent.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class SliceEntry;
    class SliceRenderer;
  }

  namespace System
  {
    class SplashSystem : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      SplashSystem(Rendering::SliceRenderer& sliceRenderer);
      ~SplashSystem();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

    protected:
      // Cached entries
      Rendering::SliceRenderer&                           m_sliceRenderer;

      uint64                                              m_sliceToken = INVALID_TOKEN;
      std::shared_ptr<Rendering::SliceEntry>              m_sliceEntry = nullptr;

      Windows::Foundation::Numerics::float3               m_position;

      float                                               m_welcomeTimerSec = 0.f;
      std::wstring                                        m_splashImageFilename = L"Assets\\Images\\HoloIntervention.png";

      static const float                                  LERP_RATE;
      static const float                                  WELCOME_DISPLAY_TIME_SEC;
    };
  }
}