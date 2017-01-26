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

// Local includes
#include "pch.h"
#include "Common.h"
#include "SplashSystem.h"
#include "StepTimer.h"

// Rendering includes
#include "SliceRenderer.h"

// STL includes
#include <functional>

// WinRT includes
#include <ppltasks.h>

// DirectXTK includes
#include <WICTextureLoader.h>

using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const float SplashSystem::LERP_RATE = 4.0;
    const float SplashSystem::NOTIFICATION_DISTANCE_OFFSET = 2.0f;
    const float3 SplashSystem::NOTIFICATION_SCREEN_OFFSET = float3(0.f, -0.11f, 0.f);

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedPosition() const
    {
      auto& pose = m_sliceEntry->GetCurrentPose();
      return float3(pose.m41, pose.m42, pose.m43);
    }

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedNormal() const
    {
      return ExtractNormal(m_sliceEntry->GetCurrentPose());
    }

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedVelocity() const
    {
      return m_sliceEntry->GetStabilizedVelocity();
    }

    //----------------------------------------------------------------------------
    float SplashSystem::GetStabilizePriority() const
    {
      // Ultra high, this should be stabilized during loading
      return 4.f;
    }

    //----------------------------------------------------------------------------
    SplashSystem::SplashSystem(Rendering::SliceRenderer& sliceRenderer)
      : m_sliceRenderer(sliceRenderer)
    {
      m_sliceToken = m_sliceRenderer.AddSlice(m_splashImageFilename);
      m_sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);
    }

    //----------------------------------------------------------------------------
    SplashSystem::~SplashSystem()
    {
    }

    //----------------------------------------------------------------------------
    void SplashSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem, SpatialPointerPose^ headPose)
    {
      // Calculate world pose, ahead of face, centered
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (headPose != nullptr)
      {
        // Get the gaze direction relative to the given coordinate system.
        const float3 headPosition = headPose->Head->Position;
        const float3 headDirection = headPose->Head->ForwardDirection;

        // Offset the view to centered, lower quadrant
        const float3 offsetFromGazeAtTwoMeters = headPosition + (float3(NOTIFICATION_DISTANCE_OFFSET) * (headDirection + NOTIFICATION_SCREEN_OFFSET));
      }
    }
  }
}