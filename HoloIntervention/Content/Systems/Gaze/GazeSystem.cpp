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
#include "GazeSystem.h"
#include "StepTimer.h"

// Physics includes
#include "PhysicsAPI.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <WindowsNumerics.h>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const std::wstring GazeSystem::GAZE_CURSOR_ASSET_LOCATION = L"Assets/Models/gaze_cursor.cmo";
    const float GazeSystem::LERP_RATE = 6.f;

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetStabilizedPosition() const
    {
      return transform(float3(0.f, 0.f, 0.f), m_modelEntry->GetCurrentPose());
    }

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetStabilizedNormal(SpatialPointerPose^ pose) const
    {
      return ExtractNormal(m_modelEntry->GetCurrentPose());
    }

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetStabilizedVelocity() const
    {
      return m_modelEntry->GetVelocity();
    }

    //----------------------------------------------------------------------------
    float GazeSystem::GetStabilizePriority() const
    {
      if (IsCursorEnabled() && !m_modelEntry->IsInFrustum())
      {
        return PRIORITY_NOT_ACTIVE;
      }
      return IsCursorEnabled() ? 1.f : PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    GazeSystem::GazeSystem(NotificationSystem& notificationSystem, Physics::PhysicsAPI& physicsAPI, Rendering::ModelRenderer& modelRenderer)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
      , m_physicsAPI(physicsAPI)
      , m_modelEntry(nullptr)
      , m_modelToken(0)
    {
      m_modelToken = m_modelRenderer.AddModel(GAZE_CURSOR_ASSET_LOCATION);
      m_modelEntry = m_modelRenderer.GetModel(m_modelToken);
      m_modelEntry->SetVisible(false);
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    GazeSystem::~GazeSystem()
    {
      m_componentReady = false;
    }

    //----------------------------------------------------------------------------
    void GazeSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ currentCoordinateSystem, SpatialPointerPose^ headPose)
    {
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (!IsCursorEnabled())
      {
        return;
      }

      float3 outHitPosition;
      float3 outHitNormal;
      float3 outHitEdge;
      bool hit = m_physicsAPI.TestRayIntersection(currentCoordinateSystem,
                 headPose->Head->Position,
                 headPose->Head->ForwardDirection,
                 outHitPosition,
                 outHitNormal,
                 outHitEdge);

      if (hit)
      {
        if (!m_hadHit)
        {
          m_modelEntry->RenderDefault();
        }
        // Update the gaze system with the pose to render
        m_currentNormal = outHitNormal;
        m_currentPosition = outHitPosition;
        m_currentEdge = outHitEdge;
      }
      else
      {
        if (m_hadHit)
        {
          m_modelEntry->RenderGreyscale();
        }
        // Couldn't find a hit, throw the cursor where the gaze head vector is at 2m depth, and turn the model grey
        m_currentPosition = headPose->Head->Position + (2.f * (headPose->Head->ForwardDirection));
        m_currentNormal = -headPose->Head->ForwardDirection;
        m_currentEdge = { 1.f, 0.f, 0.f }; // right relative to head pose
      }

      m_lastPosition = m_currentPosition;

      float3 iVec(m_currentEdge);
      float3 kVec(m_currentNormal);
      float3 jVec = -cross(iVec, kVec);
      iVec = normalize(iVec);
      float4x4 matrix = make_float4x4_world(m_currentPosition, kVec, jVec);
      m_modelEntry->SetDesiredPose(matrix);

      auto oneOverDeltaTime = 1.f / static_cast<float>(timer.GetElapsedSeconds());
      const float3 deltaPosition = m_currentPosition - m_lastPosition; // meters
      m_velocity = deltaPosition * oneOverDeltaTime; // meters per second
    }

    //----------------------------------------------------------------------------
    void GazeSystem::EnableCursor(bool enable)
    {
      m_modelEntry->SetVisible(enable);
    }

    //----------------------------------------------------------------------------
    bool GazeSystem::IsCursorEnabled() const
    {
      return m_modelEntry->IsVisible();
    }

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetHitPosition() const
    {
      return m_currentPosition;
    }

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetHitNormal() const
    {
      return m_currentNormal;
    }

    //----------------------------------------------------------------------------
    float3 GazeSystem::GetHitVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void GazeSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"show cursor"] = [this](SpeechRecognitionResult ^ result)
      {
        EnableCursor(true);
        m_notificationSystem.QueueMessage(L"Cursor on.");
      };

      callbackMap[L"hide cursor"] = [this](SpeechRecognitionResult ^ result)
      {
        EnableCursor(false);
        m_notificationSystem.QueueMessage(L"Cursor off.");
      };
    }
  }
}