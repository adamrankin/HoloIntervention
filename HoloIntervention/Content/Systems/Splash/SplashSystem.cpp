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

namespace
{
  static const float NOTIFICATION_DISTANCE_OFFSET = 2.5f;
}

namespace HoloIntervention
{
  namespace System
  {
    const float SplashSystem::LERP_RATE = 4.f;
    const float SplashSystem::WELCOME_DISPLAY_TIME_SEC = 6.f;

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      auto& slicePose = m_sliceEntry->GetCurrentPose();
      return float3(slicePose.m41, slicePose.m42, slicePose.m43);
    }

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedNormal(SpatialPointerPose^ pose) const
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
      if (m_sliceEntry == nullptr)
      {
        return PRIORITY_NOT_ACTIVE;
      }

      // Ultra high, this should be stabilized during loading
      return !m_componentReady ? 4.f : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    SplashSystem::SplashSystem(Rendering::SliceRenderer& sliceRenderer)
      : m_sliceRenderer(sliceRenderer)
    {
      create_task([this]()
      {
        while (m_sliceToken == INVALID_TOKEN)
        {
          m_sliceToken = m_sliceRenderer.AddSlice(m_splashImageFilename);
          m_sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);

          if (m_sliceToken == INVALID_TOKEN)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }
      });
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

      if (m_sliceEntry == nullptr)
      {
        // Slice hasn't been loaded yet
        return;
      }

      m_welcomeTimerSec += deltaTime;

      if (!m_componentReady && m_welcomeTimerSec >= WELCOME_DISPLAY_TIME_SEC)
      {
        m_welcomeTimerSec = 0.0;
        m_componentReady = true;
        m_sliceEntry->SetVisible(false);
        m_sliceEntry = nullptr;
        m_sliceRenderer.RemoveSlice(m_sliceToken);
        return;
      }

      if (headPose != nullptr && m_sliceEntry != nullptr)
      {
        const float3 offsetFromGazeAtTwoMeters = headPose->Head->Position + (float3(NOTIFICATION_DISTANCE_OFFSET) * headPose->Head->ForwardDirection);
        const float4x4 worldTransform = make_float4x4_world(offsetFromGazeAtTwoMeters, headPose->Head->ForwardDirection, float3(0.f, 1.f, 0.f));
        const float4x4 scaleTransform = make_float4x4_scale(1.f, 1349.f / 3836.f, 1.f); // Keep aspect ratio of 3836x1349

        if (m_firstFrame)
        {
          m_sliceEntry->ForceCurrentPose(scaleTransform * worldTransform);
          m_firstFrame = false;
        }
        else
        {
          m_sliceEntry->SetDesiredPose(scaleTransform * worldTransform);
        }
      }
    }
  }
}