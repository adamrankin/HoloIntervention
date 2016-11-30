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
#include "NotificationSystem.h"

// Common includes
#include "DeviceResources.h"
#include "StepTimer.h"

using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    const XMFLOAT4 NotificationSystem::HIDDEN_ALPHA_VALUE = XMFLOAT4(0.f, 0.f, 0.f, 0.f);
    const XMFLOAT4 NotificationSystem::SHOWING_ALPHA_VALUE = XMFLOAT4(1.f, 1.f, 1.f, 1.f);
    const double NotificationSystem::DEFAULT_NOTIFICATION_DURATION_SEC = 1.5;
    const double NotificationSystem::MAXIMUM_REQUESTED_DURATION_SEC = 10.0;
    const float NotificationSystem::LERP_RATE = 4.0;
    const float NotificationSystem::MAX_FADE_TIME = 1.f;
    const float NotificationSystem::NOTIFICATION_DISTANCE_OFFSET = 2.0f;
    const float3 NotificationSystem::NOTIFICATION_SCREEN_OFFSET = float3(0.f, -0.11f, 0.f);

    //----------------------------------------------------------------------------
    NotificationSystem::NotificationSystem(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
      , m_notificationRenderer(std::make_unique<Rendering::NotificationRenderer>(deviceResources))
    {
    }

    //----------------------------------------------------------------------------
    NotificationSystem::~NotificationSystem()
    {
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::QueueMessage(const std::string& message, double duration)
    {
      QueueMessage(std::wstring(message.begin(), message.end()), duration);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::QueueMessage(Platform::String^ message, double duration)
    {
      QueueMessage(std::wstring(message->Data()), duration);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::QueueMessage(const std::wstring& message, double duration)
    {
      duration = clamp<double>(duration, MAXIMUM_REQUESTED_DURATION_SEC, 0.1);

      std::lock_guard<std::mutex> guard(m_messageQueueMutex);
      MessageDuration mt(message, duration);
      m_messages.push_back(mt);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::DebugSetMessage(const std::wstring& message, double duration /*= DEFAULT_NOTIFICATION_DURATION_SEC*/)
    {
      // set this message as the active one
      m_animationState = HIDDEN;
      m_messageTimeElapsedSec = 0.0;

      std::lock_guard<std::mutex> guard(m_messageQueueMutex);
      MessageDuration mt(message, duration);
      m_messages.push_front(mt);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::DebugSetMessage(Platform::String^ message, double duration /*= DEFAULT_NOTIFICATION_DURATION_SEC*/)
    {
      std::wstring string(message->Data());
      DebugSetMessage(string, duration);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::Initialize(SpatialPointerPose^ pointerPose)
    {
      SetPose(pointerPose);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::Update(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer)
    {
      // The following code updates any relevant timers depending on state
      auto elapsedTimeSec = timer.GetElapsedSeconds();

      if (m_animationState == SHOWING)
      {
        // Accumulate the total time shown
        m_messageTimeElapsedSec += elapsedTimeSec;
      }

      // The following code manages state transition
      if (m_animationState == HIDDEN && m_messages.size() > 0)
      {
        // We had nothing showing, and a new message has come in

        // Force the position to be in front of the user as the last pose is wherever the previous message stopped showing in world space
        m_position = pointerPose->Head->Position + (float3(NOTIFICATION_DISTANCE_OFFSET) * (pointerPose->Head->ForwardDirection + NOTIFICATION_SCREEN_OFFSET));

        m_animationState = FADING_IN;
        m_fadeTime = MAX_FADE_TIME;

        GrabNextMessage();
      }
      else if (m_animationState == SHOWING && m_messageTimeElapsedSec > m_currentMessage.second)
      {
        // The time for the current message has ended

        if (m_messages.size() > 0)
        {
          // There is a new message to show, switch to it, do not do any fade
          GrabNextMessage();

          // Reset timer for new message
          m_messageTimeElapsedSec = 0.0;
        }
        else
        {
          m_animationState = FADING_OUT;
          m_fadeTime = MAX_FADE_TIME;
        }
      }
      else if (m_animationState == FADING_IN)
      {
        if (!IsFading())
        {
          // animation has finished, switch to showing
          m_animationState = SHOWING;
          m_messageTimeElapsedSec = 0.f;
        }
      }
      else if (m_animationState == FADING_OUT)
      {
        if (m_messages.size() > 0)
        {
          // A message has come in while we were fading out, reverse and fade back in
          GrabNextMessage();

          m_animationState = FADING_IN;
          m_fadeTime = MAX_FADE_TIME - m_fadeTime; // reverse the fade
        }

        if (!IsFading())
        {
          // animation has finished, switch to HIDDEN
          m_animationState = HIDDEN;
        }
      }

      if (IsShowingNotification())
      {
        UpdateHologramPosition(pointerPose, timer);

        CalculateWorldMatrix();
        CalculateAlpha(timer);
        CalculateVelocity(1.f / static_cast<float>(timer.GetElapsedSeconds()));
      }

      m_notificationRenderer->Update(m_constantBuffer);
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::CreateDeviceDependentResources()
    {
      m_notificationRenderer->CreateDeviceDependentResources();
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;
      m_notificationRenderer->ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::CalculateAlpha(const DX::StepTimer& timer)
    {
      const float deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (IsFading())
      {
        // Fade the quad in, or out.
        if (m_animationState == FADING_IN)
        {
          const float fadeLerp = 1.f - (m_fadeTime / MAX_FADE_TIME);
          m_constantBuffer.hologramColorFadeMultiplier = XMFLOAT4(fadeLerp, fadeLerp, fadeLerp, 1.f);
        }
        else
        {
          const float fadeLerp = (m_fadeTime / MAX_FADE_TIME);
          m_constantBuffer.hologramColorFadeMultiplier = XMFLOAT4(fadeLerp, fadeLerp, fadeLerp, 1.f);
        }
        m_fadeTime -= deltaTime;
      }
      else
      {
        m_constantBuffer.hologramColorFadeMultiplier = (m_animationState == SHOWING ? SHOWING_ALPHA_VALUE : HIDDEN_ALPHA_VALUE);
      }
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::CalculateWorldMatrix()
    {
      XMVECTOR facingNormal = XMVector3Normalize(-XMLoadFloat3(&m_position));
      XMVECTOR xAxisRotation = XMVector3Normalize(XMVectorSet(XMVectorGetZ(facingNormal), 0.f, -XMVectorGetX(facingNormal), 0.f));
      XMVECTOR yAxisRotation = XMVector3Normalize(XMVector3Cross(facingNormal, xAxisRotation));

      // Construct the 4x4 rotation matrix.
      XMMATRIX rotationMatrix = XMMATRIX(xAxisRotation, yAxisRotation, facingNormal, XMVectorSet(0.f, 0.f, 0.f, 1.f));
      const XMMATRIX modelTranslation = XMMatrixTranslationFromVector(XMLoadFloat3(&m_position));
      XMStoreFloat4x4(&m_constantBuffer.worldMatrix, XMMatrixTranspose(rotationMatrix * modelTranslation));
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::GrabNextMessage()
    {
      if (m_messages.size() == 0)
      {
        return;
      }
      m_currentMessage = m_messages.front();
      m_messages.pop_front();

      m_notificationRenderer->RenderText(m_currentMessage.first);
    }

    //----------------------------------------------------------------------------
    bool NotificationSystem::IsFading() const
    {
      return m_fadeTime > 0.f;
    }

    //----------------------------------------------------------------------------
    bool NotificationSystem::IsShowingNotification() const
    {
      return m_animationState != HIDDEN;
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::UpdateHologramPosition(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer)
    {
      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (pointerPose != nullptr)
      {
        // Get the gaze direction relative to the given coordinate system.
        const float3 headPosition = pointerPose->Head->Position;
        const float3 headDirection = pointerPose->Head->ForwardDirection;

        // Offset the view to centered, lower quadrant
        const float3 offsetFromGazeAtTwoMeters = headPosition + (float3(NOTIFICATION_DISTANCE_OFFSET) * (headDirection + NOTIFICATION_SCREEN_OFFSET));

        // Use linear interpolation to smooth the position over time
        const float3 smoothedPosition = lerp(m_position, offsetFromGazeAtTwoMeters, deltaTime * LERP_RATE);

        // This will be used as the translation component of the hologram's model transform.
        m_lastPosition = m_position;
        m_position = smoothedPosition;
      }
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::SetPose(SpatialPointerPose^ pointerPose)
    {
      const float3 headPosition = pointerPose->Head->Position;
      const float3 headDirection = pointerPose->Head->ForwardDirection;

      m_lastPosition = m_position = headPosition + (float3(NOTIFICATION_DISTANCE_OFFSET) * (headDirection + NOTIFICATION_SCREEN_OFFSET));
    }

    //----------------------------------------------------------------------------
    std::unique_ptr<Rendering::NotificationRenderer>& NotificationSystem::GetRenderer()
    {
      return m_notificationRenderer;
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {

    }

    //----------------------------------------------------------------------------
    const float3& NotificationSystem::GetPosition() const
    {
      return m_position;
    }

    //----------------------------------------------------------------------------
    const float3& NotificationSystem::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void NotificationSystem::CalculateVelocity(float oneOverDeltaTime)
    {
      const float3 deltaPosition = m_position - m_lastPosition; // meters
      m_velocity = deltaPosition * oneOverDeltaTime; // meters per second
    }
  }
}