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
#include "NotificationsAPI.h"

using namespace Windows::Foundation::Numerics;

namespace TrackedUltrasound
{
  namespace Notifications
  {
    const double NotificationsAPI::MAXIMUM_REQUESTED_DURATION_SEC = 10.0;
    const double NotificationsAPI::DEFAULT_NOTIFICATION_DURATION_SEC = 1.5;
    const DirectX::XMFLOAT4 NotificationsAPI::SHOWING_ALPHA_VALUE = XMFLOAT4(1.f, 1.f, 1.f, 1.f);
    const DirectX::XMFLOAT4 NotificationsAPI::HIDDEN_ALPHA_VALUE = XMFLOAT4(0.f, 0.f, 0.f, 0.f);
    const Windows::Foundation::Numerics::float3 NotificationsAPI::NOTIFICATION_SCREEN_OFFSET = float3(0.f, -0.13f, 0.f);
    const float NotificationsAPI::NOTIFICATION_DISTANCE_OFFSET = 2.2f;
    const float NotificationsAPI::LERP_RATE = 4.0;

    //----------------------------------------------------------------------------
    NotificationsAPI::NotificationsAPI(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
      , m_notificationRenderer(std::make_unique<Rendering::NotificationRenderer>(deviceResources))
    {
    }

    //----------------------------------------------------------------------------
    NotificationsAPI::~NotificationsAPI()
    {
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::QueueMessage(const std::string& message, double duration)
    {
      this->QueueMessage(std::wstring(message.begin(), message.end()), duration);
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::QueueMessage(Platform::String^ message, double duration)
    {
      this->QueueMessage(std::wstring(message->Data()), duration);
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::QueueMessage(const std::wstring& message, double duration)
    {
      duration = clamp<double>(duration, MAXIMUM_REQUESTED_DURATION_SEC, 0.1);

      std::lock_guard<std::mutex> guard(m_messageQueueMutex);
      MessageDuration mt(message, duration);
      m_messages.push_back(mt);
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::Initialize(SpatialPointerPose^ pointerPose)
    {
      SetPose(pointerPose);
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::Update(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer)
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
        m_animationState = FADING_IN;
        m_fadeTime = c_maxFadeTime;

        GrabNextMessage();
      }
      else if (m_animationState == SHOWING && m_messageTimeElapsedSec > m_currentMessage.second)
      {
        // The time for the current message has ended

        if (m_messages.size() > 0)
        {
          // There is a new message to show, switch to it, do not do any fade
          // TODO : in the future, add a blink animation of some type
          GrabNextMessage();

          // Reset timer for new message
          m_messageTimeElapsedSec = 0.0;
        }
        else
        {
          m_animationState = FADING_OUT;
          m_fadeTime = c_maxFadeTime;
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
          m_fadeTime = c_maxFadeTime - m_fadeTime; // reverse the fade
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
    void NotificationsAPI::CreateDeviceDependentResources()
    {
      m_notificationRenderer->CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::ReleaseDeviceDependentResources()
    {
      m_notificationRenderer->ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::CalculateAlpha(const DX::StepTimer& timer)
    {
      const float deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      if (IsFading())
      {
        // Fade the quad in, or out.
        if (m_animationState == FADING_IN)
        {
          const float fadeLerp = 1.f - (m_fadeTime / c_maxFadeTime);
          m_constantBuffer.hologramColorFadeMultiplier = XMFLOAT4(fadeLerp, fadeLerp, fadeLerp, 1.f);
        }
        else
        {
          const float fadeLerp = (m_fadeTime / c_maxFadeTime);
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
    void NotificationsAPI::CalculateWorldMatrix()
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
    void NotificationsAPI::GrabNextMessage()
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
    bool NotificationsAPI::IsFading() const
    {
      return m_fadeTime > 0.f;
    }

    //----------------------------------------------------------------------------
    bool NotificationsAPI::IsShowingNotification() const
    {
      return m_animationState != HIDDEN;
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::UpdateHologramPosition(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer)
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
    void NotificationsAPI::SetPose(SpatialPointerPose^ pointerPose)
    {
      const float3 headPosition = pointerPose->Head->Position;
      const float3 headDirection = pointerPose->Head->ForwardDirection;

      m_lastPosition = m_position = headPosition + (float3(NOTIFICATION_DISTANCE_OFFSET) * (headDirection + NOTIFICATION_SCREEN_OFFSET));
    }

    //----------------------------------------------------------------------------
    std::unique_ptr<Rendering::NotificationRenderer>& NotificationsAPI::GetRenderer()
    {
      return m_notificationRenderer;
    }

    //----------------------------------------------------------------------------
    const float3& NotificationsAPI::GetPosition() const
    {
      return m_position;
    }

    //----------------------------------------------------------------------------
    const float3& NotificationsAPI::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void NotificationsAPI::CalculateVelocity(float oneOverDeltaTime)
    {
      const float3 deltaPosition = m_position - m_lastPosition; // meters
      m_velocity = deltaPosition * oneOverDeltaTime; // meters per second
    }
  }
}