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
#include "AppView.h"
#include "Common.h"
#include "GazeSystem.h"

// Common includes
#include "StepTimer.h"

// Rendering includes
#include "ModelRenderer.h"

// System includes
#include "SpatialSystem.h"
#include "NotificationSystem.h"

// WinRT includes
#include <WindowsNumerics.h>

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace System
  {
    const std::wstring GazeSystem::GAZE_CURSOR_ASSET_LOCATION = L"Assets/Models/gaze_cursor.cmo";
    const float GazeSystem::LERP_RATE = 6.f;

    //----------------------------------------------------------------------------
    GazeSystem::GazeSystem()
      : m_modelEntry(nullptr)
      , m_modelToken(0)
    {
      m_modelToken = HoloIntervention::instance()->GetModelRenderer().AddModel(GAZE_CURSOR_ASSET_LOCATION);
      m_modelEntry = HoloIntervention::instance()->GetModelRenderer().GetModel(m_modelToken);
      m_modelEntry->SetVisible(false);
    }

    //----------------------------------------------------------------------------
    GazeSystem::~GazeSystem()
    {
    }

    //----------------------------------------------------------------------------
    void GazeSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ currentCoordinateSystem, SpatialPointerPose^ headPose)
    {
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      // TODO : add future kalman filtering to smooth cursor position
      if (IsCursorEnabled())
      {
        float3 outHitPosition;
        float3 outHitNormal;
        float3 outHitEdge;
        bool hit = HoloIntervention::instance()->GetSpatialSystem().TestRayIntersection(currentCoordinateSystem,
                   headPose->Head->Position,
                   headPose->Head->ForwardDirection,
                   outHitPosition,
                   outHitNormal,
                   outHitEdge);

        if (hit)
        {
          // Update the gaze system with the pose to render
          m_goalHitNormal = outHitNormal;
          m_goalHitPosition = outHitPosition;
          m_goalHitEdge = outHitEdge;
          m_modelEntry->RenderDefault();
        }
        else
        {
          // Couldn't find a hit, throw the cursor where the gaze head vector is at 2m depth, and turn the model grey
          m_goalHitPosition = headPose->Head->Position + (2.f * (headPose->Head->ForwardDirection));
          m_goalHitNormal = -headPose->Head->ForwardDirection;
          m_goalHitEdge = { 1.f, 0.f, 0.f }; // right relative to head pose
          m_modelEntry->RenderGreyscale();
        }
      }

      m_lastPosition = m_currentPosition;
      m_currentPosition = lerp(m_currentPosition, m_goalHitPosition, deltaTime * LERP_RATE);
      m_currentNormal = lerp(m_currentNormal, m_goalHitNormal, deltaTime * LERP_RATE);
      m_currentEdge = lerp(m_currentEdge, m_goalHitEdge, deltaTime * LERP_RATE);

      float3 iVec(m_currentEdge);
      float3 kVec(m_currentNormal);
      float3 jVec = -cross(iVec, kVec);
      iVec = normalize(iVec);
      float4x4 matrix = make_float4x4_world(m_currentPosition, kVec, jVec);
      m_modelEntry->SetWorld(matrix);

      CalculateVelocity(1.f / static_cast<float>(timer.GetElapsedSeconds()));
    }

    //----------------------------------------------------------------------------
    void GazeSystem::EnableCursor(bool enable)
    {
      m_systemEnabled = enable;

      m_modelEntry->SetVisible(enable);
    }

    //----------------------------------------------------------------------------
    bool GazeSystem::IsCursorEnabled()
    {
      return m_systemEnabled;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& GazeSystem::GetHitPosition() const
    {
      return m_goalHitPosition;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& GazeSystem::GetHitNormal() const
    {
      return m_goalHitNormal;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 GazeSystem::GetHitVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void GazeSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"show cursor"] = [this](SpeechRecognitionResult ^ result)
      {
        EnableCursor(true);
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Cursor on.");
      };

      callbackMap[L"hide cursor"] = [this](SpeechRecognitionResult ^ result)
      {
        EnableCursor(false);
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Cursor off.");
      };
    }

    //----------------------------------------------------------------------------
    void GazeSystem::CalculateVelocity(float oneOverDeltaTime)
    {
      const float3 deltaPosition = m_currentPosition - m_lastPosition; // meters
      m_velocity = deltaPosition * oneOverDeltaTime; // meters per second
    }
  }
}