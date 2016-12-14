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
#include "IGTLinkIF.h"
#include "NotificationSystem.h"

// Windows includes
#include <ppltasks.h>
#include <vccorlib.h>

// UWPOpenIGT includes
#include <IGTCommon.h>

// IGT includes
#include <igtlMessageBase.h>
#include <igtlStatusMessage.h>

using namespace Concurrency;
using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace Network
  {
    const double IGTLinkIF::CONNECT_TIMEOUT_SEC = 3.0;
    const uint32_t IGTLinkIF::RECONNECT_RETRY_DELAY_MSEC = 100;
    const uint32_t IGTLinkIF::RECONNECT_RETRY_COUNT = 10;

    //----------------------------------------------------------------------------
    IGTLinkIF::IGTLinkIF()
    {
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    IGTLinkIF::~IGTLinkIF()
    {
    }

    //----------------------------------------------------------------------------
    task<bool> IGTLinkIF::ConnectAsync(double timeoutSec, task_options& options)
    {
      m_connectionState = CONNECTION_STATE_CONNECTING;

      std::lock_guard<std::mutex> guard(m_clientMutex);
      auto connectTask = create_task(m_igtClient->ConnectAsync(timeoutSec), options);

      return connectTask.then([this](bool result)
      {
        if (result)
        {
          m_connectionState = CONNECTION_STATE_CONNECTED;
        }
        else
        {
          m_connectionState = CONNECTION_STATE_DISCONNECTED;
        }
        return result;
      });
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::Disconnect()
    {
      m_igtClient->Disconnect();
      m_connectionState = CONNECTION_STATE_DISCONNECTED;
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::IsConnected()
    {
      return m_connectionState == CONNECTION_STATE_CONNECTED;
    }

    //----------------------------------------------------------------------------
    HoloIntervention::Network::ConnectionState IGTLinkIF::GetConnectionState() const
    {
      return m_connectionState;
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::SetHostname(const std::wstring& hostname)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->ServerHost = ref new Platform::String(hostname.c_str());
    }

    //----------------------------------------------------------------------------
    std::wstring IGTLinkIF::GetHostname() const
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      return std::wstring(m_igtClient->ServerHost->Data());
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::SetPort(int32 port)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->ServerPort = port;
    }

    //----------------------------------------------------------------------------
    int32 IGTLinkIF::GetPort() const
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      return m_igtClient->ServerPort;
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::GetTrackedFrame(UWPOpenIGTLink::TrackedFrame^& frame, double* latestTimestamp)
    {
      double ts = latestTimestamp == nullptr ? 0.0 : *latestTimestamp;
      auto latestFrame = m_igtClient->GetTrackedFrame(ts);
      if (latestFrame == nullptr)
      {
        return false;
      }
      frame = latestFrame;
      *latestTimestamp = frame->Timestamp;
      return true;
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::GetCommand(UWPOpenIGTLink::Command^& cmd, double* latestTimestamp)
    {
      double ts = latestTimestamp == nullptr ? 0.0 : *latestTimestamp;
      auto latestCommand =  m_igtClient->GetCommand(ts);
      if (latestCommand == nullptr)
      {
        return false;
      }
      cmd = latestCommand;
      *latestTimestamp = cmd->Timestamp;
      return true;
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"connect"] = [this](SpeechRecognitionResult ^ result)
      {
        uint64 connectMessageId = HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connecting...");
        ConnectAsync(4.0).then([this, connectMessageId](bool result)
        {
          HoloIntervention::instance()->GetNotificationSystem().RemoveMessage(connectMessageId);
          if (result)
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connection successful.");
          }
          else
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connection failed.");
          }

          if (result)
          {
            m_keepAliveTask = &KeepAliveAsync();
          }
        }, concurrency::task_continuation_context::use_arbitrary());
      };

      callbackMap[L"disconnect"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_keepAliveTask != nullptr)
        {
          m_keepAliveTokenSource.cancel();
          m_keepAliveTask = nullptr;
        }

        Disconnect();
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Disconnected.");
      };
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte> IGTLinkIF::GetSharedImagePtr(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      return *(std::shared_ptr<byte>*)frame->ImageDataSharedPtr;
    }

    //----------------------------------------------------------------------------
    task<void> IGTLinkIF::KeepAliveAsync()
    {
      m_keepAliveTokenSource = cancellation_token_source();
      auto token = m_keepAliveTokenSource.get_token();
      return create_task([this, token]()
      {
        while (!token.is_canceled())
        {
          if (m_connectionState == CONNECTION_STATE_CONNECTED)
          {
            // send keep alive message
            igtl::StatusMessage::Pointer statusMsg = igtl::StatusMessage::New();
            statusMsg->SetCode(igtl::StatusMessage::STATUS_OK);
            statusMsg->Pack();
            bool result(false);
            {
              std::lock_guard<std::mutex> guard(m_clientMutex);
              RETRY_UNTIL_TRUE(result = m_igtClient->SendMessage((UWPOpenIGTLink::MessageBasePointerPtr)&statusMsg), 10, 25);
            }
            if (!result)
            {
              Disconnect();
              m_connectionState = CONNECTION_STATE_CONNECTION_LOST;
              if (m_reconnectOnDrop)
              {
                uint32_t retryCount(0);
                while (m_connectionState != CONNECTION_STATE_CONNECTED && retryCount < RECONNECT_RETRY_COUNT)
                {
                  // Either it's up and running and it can connect right away, or it's down and will never connect
                  auto connectTask = ConnectAsync(0.1, task_options(task_continuation_context::use_arbitrary())).then([this, &retryCount](task<bool> previousTask)
                  {
                    bool result = previousTask.get();

                    if (!result)
                    {
                      std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_RETRY_DELAY_MSEC));
                      retryCount++;
                    }
                  }, task_continuation_context::use_arbitrary());

                  try
                  {
                    connectTask.wait();
                  }
                  catch (const std::exception& e)
                  {
                    OutputDebugStringA(e.what());
                  }
                  catch (Platform::Exception^ e)
                  {
                    OutputDebugStringW(e->Message->Data());
                  }
                }

                if (m_connectionState != CONNECTION_STATE_CONNECTED)
                {
                  m_keepAliveTokenSource.cancel();
                  m_keepAliveTask = nullptr;
                  HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connection lost. Check server.");
                  return;
                }
              }
              else
              {
                // Don't reconnect on drop
                m_keepAliveTokenSource.cancel();
                m_keepAliveTask = nullptr;
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connection lost. Check server.");
                return;
              }
            }
          }
          else
          {
            OutputDebugStringA("Keep alive running unconnected but token not canceled.\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        }
      }, task_options(token, task_continuation_context::use_arbitrary()));
    }
  }
}