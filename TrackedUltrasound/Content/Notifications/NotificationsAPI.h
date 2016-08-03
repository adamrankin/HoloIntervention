#pragma once
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

// Common Includes
#include "AppView.h"
#include "DeviceResources.h"
#include "StepTimer.h"

// Rendering includes
#include "NotificationRenderer.h"

// STD includes
#include <deque>

namespace TrackedUltrasound
{
  namespace Notifications
  {
    class NotificationsAPI
    {
      enum AnimationState
      {
        SHOWING,
        FADING_IN,
        FADING_OUT,
        HIDDEN
      };

      typedef std::pair<std::wstring, double> MessageDuration;
      typedef std::deque<MessageDuration> MessageQueue;

    public:
      NotificationsAPI(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~NotificationsAPI();

      // Add a message to the queue to render
      void QueueMessage(const std::string& message, double duration = DEFAULT_NOTIFICATION_DURATION_SEC);
      void QueueMessage(const std::wstring& message, double duration = DEFAULT_NOTIFICATION_DURATION_SEC);
      void QueueMessage(Platform::String^ message, double duration = DEFAULT_NOTIFICATION_DURATION_SEC);

      void Initialize(SpatialPointerPose^ pointerPose);
      void Update(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer);

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Accessors
      bool IsShowingNotification() const;
      const float3& GetPosition() const;
      const float3& GetVelocity() const;

      // Override the current lerp and force the position
      void SetPose(SpatialPointerPose^ pointerPose);

      std::unique_ptr<Rendering::NotificationRenderer>& GetRenderer();

    protected:
      void UpdateHologramPosition(SpatialPointerPose^ pointerPose, const DX::StepTimer& timer);

      void CalculateWorldMatrix();
      void CalculateAlpha(const DX::StepTimer& timer);
      void CalculateVelocity(float oneOverDeltaTime);
      void GrabNextMessage();
      bool IsFading() const;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;

      // Renderer
      std::unique_ptr<Rendering::NotificationRenderer>    m_notificationRenderer;

      // Constant buffer data cache
      Rendering::NotificationConstantBuffer               m_constantBuffer;

      // Number of seconds it takes to fade the hologram in, or out.
      const float                                         c_maxFadeTime = 1.f;

      // Timer used to fade the hologram in, or out.
      float                                               m_fadeTime = 0.f;

      // Whether or not the hologram is fading in, or out.
      AnimationState                                      m_animationState = HIDDEN;

      // Message pose information
      float3                                              m_position = { 0.f, 0.f, -2.f };
      float3                                              m_lastPosition = { 0.f, 0.f, -2.f };
      float3                                              m_velocity = { 0.f, 0.f, 0.f };

      // List of messages to show, in order (fifo)
      MessageQueue                                        m_messages;
      MessageDuration                                     m_currentMessage;

      // Lock protection when accessing message list
      std::mutex                                          m_messageQueueMutex;

      // Cached value of the total time the current message has been showing
      double                                              m_messageTimeElapsedSec = 0.0f;

      // Constants relating to behavior of the notification system
      static const double                                 MAXIMUM_REQUESTED_DURATION_SEC;
      static const double                                 DEFAULT_NOTIFICATION_DURATION_SEC;
      static const uint32                                 BLUR_TARGET_WIDTH_PIXEL;
      static const uint32                                 OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL;
      static const XMFLOAT4                               SHOWING_ALPHA_VALUE;
      static const XMFLOAT4                               HIDDEN_ALPHA_VALUE;
      static const float3                                 NOTIFICATION_SCREEN_OFFSET;
      static const float                                  NOTIFICATION_DISTANCE_OFFSET;
      static const float                                  LERP_RATE;
    };
  }
}