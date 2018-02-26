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
    const float SplashSystem::MINIMUM_WELCOME_DISPLAY_TIME_SEC = 6.f;

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      auto& slicePose = m_sliceEntry->GetCurrentPose();
      return float3(slicePose.m41, slicePose.m42, slicePose.m43);
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
      return !m_componentReady ? PRIORITY_SPLASH : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    SplashSystem::SplashSystem(Rendering::SliceRenderer& sliceRenderer)
      : m_sliceRenderer(sliceRenderer)
    {
      m_sliceRenderer.AddSliceAsync(m_splashImageFilename, float4x4::identity(), true).then([this](uint64 entryId)
      {
        m_sliceToken = entryId;
        m_sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);
        m_sliceEntry->SetScalingFactor(1.f, 1349.f / 3836.f);
        m_sliceEntry->SetUseHeadUpDirection(false);
        m_sliceEntry->SetHeadlocked(true);
      });
    }

    //----------------------------------------------------------------------------
    SplashSystem::~SplashSystem()
    {
    }

    //----------------------------------------------------------------------------
    void SplashSystem::StartSplash()
    {
      if (m_sliceEntry != nullptr)
      {
        m_sliceEntry->SetVisible(true);
      }
    }

    //----------------------------------------------------------------------------
    void SplashSystem::EndSplash()
    {
      if (m_sliceEntry != nullptr)
      {
        m_sliceEntry->SetVisible(false);
      }
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

      if (!m_componentReady && m_welcomeTimerSec >= MINIMUM_WELCOME_DISPLAY_TIME_SEC)
      {
        m_componentReady = true;
        return;
      }
    }
  }
}