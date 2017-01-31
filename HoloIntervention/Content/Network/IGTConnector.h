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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// STL includes
#include <mutex>

namespace igtl
{
  class TrackedFrameMessage;
}

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
  }

  namespace Network
  {
    enum ConnectionState
    {
      CONNECTION_STATE_UNKNOWN,
      CONNECTION_STATE_CONNECTING,
      CONNECTION_STATE_CONNECTION_LOST,
      CONNECTION_STATE_DISCONNECTING,
      CONNECTION_STATE_DISCONNECTED,
      CONNECTION_STATE_CONNECTED
    };

    class IGTConnector : public Sound::IVoiceInput, public IEngineComponent
    {
    public:
      IGTConnector(System::NotificationSystem& notificationSystem);
      ~IGTConnector();

      /// Connect to the server specified by SetHostname() and SetPort()
      /// If connected to a server, disconnects first.
      Concurrency::task<bool> ConnectAsync(double timeoutSec = CONNECT_TIMEOUT_SEC, Concurrency::task_options& options = Concurrency::task_options());

      void Disconnect();
      bool IsConnected();
      ConnectionState GetConnectionState() const;

      Concurrency::task<std::vector<std::string>> FindServersAsync();
      void SetHostname(const std::wstring& hostname);
      std::wstring GetHostname() const;

      void SetPort(int32 port);
      int32 GetPort() const;

      bool GetTrackedFrame(UWPOpenIGTLink::TrackedFrame^& frame, double* latestTimestamp = nullptr);
      bool GetCommand(UWPOpenIGTLink::Command^& cmd, double* latestTimestamp = nullptr);

      /// IVoiceInput functions
      void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    public:
      // Static helper functions
      static std::shared_ptr<byte> GetSharedImagePtr(UWPOpenIGTLink::TrackedFrame^ frame);

    protected:
      Concurrency::task<void> KeepAliveAsync();

    protected:
      // Cached entries
      System::NotificationSystem&                   m_notificationSystem;

      std::map<double, bool>                        m_processedFrames;
      UWPOpenIGTLink::IGTLinkClient^                m_igtClient = ref new UWPOpenIGTLink::IGTLinkClient();
      ConnectionState                               m_connectionState = CONNECTION_STATE_UNKNOWN;
      mutable std::mutex                            m_clientMutex;
      Concurrency::task<void>*                      m_keepAliveTask = nullptr;
      Concurrency::cancellation_token_source        m_keepAliveTokenSource;
      bool                                          m_reconnectOnDrop = true;

      // Constants relating to IGT behavior
      static const double                           CONNECT_TIMEOUT_SEC;
      static const uint32                           RECONNECT_RETRY_DELAY_MSEC;
      static const uint32                           RECONNECT_RETRY_COUNT;
      static const uint32                           PROCESSED_FRAMES_MAX_SIZE;
    };
  }
}