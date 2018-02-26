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

// Local includes
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelEntry;
    class ModelRenderer;
  }

  namespace Physics
  {
    class PhysicsAPI;
  }

  namespace System
  {
    class NotificationSystem;

    class GazeSystem : public Input::IVoiceInput, public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      GazeSystem(NotificationSystem& notificationSystem, Physics::PhysicsAPI& physicsAPI, Rendering::ModelRenderer& modelRenderer);
      ~GazeSystem();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ currentCoordinateSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      void EnableCursor(bool enable);
      bool IsCursorEnabled() const;

      Windows::Foundation::Numerics::float3 GetHitPosition() const;
      Windows::Foundation::Numerics::float3 GetHitNormal() const;
      Windows::Foundation::Numerics::float3 GetHitVelocity() const;

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);

    protected:
      // Cached entries
      Rendering::ModelRenderer&                 m_modelRenderer;
      NotificationSystem&                       m_notificationSystem;
      Physics::PhysicsAPI&                      m_physicsAPI;

      std::shared_ptr<Rendering::ModelEntry>    m_modelEntry;
      uint64                                    m_modelToken;

      std::atomic_bool                          m_hadHit = false;
      Windows::Foundation::Numerics::float3     m_currentPosition = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float3     m_currentNormal = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float3     m_currentEdge = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float3     m_lastPosition = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float3     m_velocity = { 0.f, 0.f, 0.f };

      static const std::wstring GAZE_CURSOR_ASSET_LOCATION;
      static const uint32 FRAMES_UNTIL_HIT_EXPIRES;
      static const float LERP_RATE;
    };
  }
}