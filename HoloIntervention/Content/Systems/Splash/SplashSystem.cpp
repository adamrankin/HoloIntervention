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

// Unnecessary, but removes intellisense errors
#include <WindowsNumerics.h>

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

      // Don't affect the actual loading of the system
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    SplashSystem::~SplashSystem()
    {
    }

    //----------------------------------------------------------------------------
    void SplashSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ hmdCoordinateSystem, SpatialPointerPose^ headPose)
    {
      const float NOTIFICATION_DISTANCE_OFFSET = 2.f;

      // Calculate world pose, ahead of face, centered
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (headPose != nullptr)
      {
        const float3 offsetFromGazeAtTwoMeters = headPose->Head->Position + (float3(NOTIFICATION_DISTANCE_OFFSET) * headPose->Head->ForwardDirection);
        const float4x4 worldTransform = make_float4x4_world(offsetFromGazeAtTwoMeters, -headPose->Head->ForwardDirection, headPose->Head->UpDirection);
        const float4x4 scaleTransform = make_float4x4_scale(0.6f, 0.3f, 1.f); // 60cm x 30cm

        m_sliceEntry->SetDesiredPose(worldTransform * scaleTransform);
      }
    }
  }
}