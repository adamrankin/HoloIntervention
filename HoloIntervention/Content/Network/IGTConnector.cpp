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
#include "IGTConnector.h"
#include "NotificationSystem.h"
#include "VoiceInput.h"

// Windows includes
#include <ppltasks.h>
#include <vccorlib.h>
#include <ppl.h>

// UWPOpenIGT includes
#include <IGTCommon.h>

// IGT includes
#include <igtlMessageBase.h>
#include <igtlStatusMessage.h>

// STL includes
#include <numeric>

using namespace Concurrency;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking;

namespace HoloIntervention
{
  namespace Network
  {
    const double IGTConnector::CONNECT_TIMEOUT_SEC = 3.0;
    const uint32_t IGTConnector::RECONNECT_RETRY_DELAY_MSEC = 100;
    const uint32_t IGTConnector::RECONNECT_RETRY_COUNT = 10;
    const uint32 IGTConnector::DICTATION_TIMEOUT_DELAY_MSEC = 8000;
    const uint32 IGTConnector::KEEP_ALIVE_INTERVAL_MSEC = 1000;

    //----------------------------------------------------------------------------
    IGTConnector::IGTConnector(System::NotificationSystem& notificationSystem)
      : m_notificationSystem(notificationSystem)
    {
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    IGTConnector::~IGTConnector()
    {
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TransformName^ IGTConnector::GetEmbeddedImageTransformName() const
    {
      return m_igtClient->EmbeddedImageTransformName;
    }

    //----------------------------------------------------------------------------
    void IGTConnector::SetEmbeddedImageTransformName(UWPOpenIGTLink::TransformName^ name)
    {
      m_igtClient->EmbeddedImageTransformName = name;
    }

    //----------------------------------------------------------------------------
    std::wstring IGTConnector::GetConnectionName() const
    {
      return m_connectionName;
    }

    //----------------------------------------------------------------------------
    void IGTConnector::SetConnectionName(const std::wstring& name)
    {
      m_connectionName = name;
    }

    //----------------------------------------------------------------------------
    task<bool> IGTConnector::ConnectAsync(bool keepAlive, double timeoutSec, task_options& options)
    {
      m_connectionState = CONNECTION_STATE_CONNECTING;

      std::lock_guard<std::mutex> guard(m_clientMutex);
      auto connectTask = create_task(m_igtClient->ConnectAsync(timeoutSec), options);

      return connectTask.then([this](task<bool> connectTask)
      {
        bool result(false);
        try
        {
          result = connectTask.get();
        }
        catch (const std::exception& e)
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("IGTConnector failed to connect: ") + e.what());
          return false;
        }

        if (result)
        {
          m_connectionState = CONNECTION_STATE_CONNECTED;
          if (result)
          {
            KeepAliveAsync().then([this](task<void> keepAliveTask)
            {
              try
              {
                keepAliveTask.wait();
              }
              catch (const std::exception& e)
              {
                LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("KeepAliveTask exception: ") + e.what());
              }
            });
          }
        }
        else
        {
          m_connectionState = CONNECTION_STATE_DISCONNECTED;
        }
        return result;
      });
    }

    //----------------------------------------------------------------------------
    void IGTConnector::Disconnect()
    {
      m_igtClient->Disconnect();
      m_keepAliveTokenSource.cancel();
      m_keepAliveTokenSource = cancellation_token_source();
      m_connectionState = CONNECTION_STATE_DISCONNECTED;
    }

    //----------------------------------------------------------------------------
    bool IGTConnector::IsConnected()
    {
      return m_connectionState == CONNECTION_STATE_CONNECTED;
    }

    //----------------------------------------------------------------------------
    Network::ConnectionState IGTConnector::GetConnectionState() const
    {
      return m_connectionState;
    }

    //----------------------------------------------------------------------------
    void IGTConnector::SetReconnectOnDrop(bool arg)
    {
      m_reconnectOnDrop = arg;
    }

    //----------------------------------------------------------------------------
    bool IGTConnector::GetReconnectOnDrop() const
    {
      return m_reconnectOnDrop;
    }

    //----------------------------------------------------------------------------
    void IGTConnector::SetHostname(const std::wstring& hostname)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->ServerHost = ref new Platform::String(hostname.c_str());
    }

    //----------------------------------------------------------------------------
    std::wstring IGTConnector::GetHostname() const
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      return std::wstring(m_igtClient->ServerHost->Data());
    }

    //----------------------------------------------------------------------------
    void IGTConnector::SetPort(int32 port)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->ServerPort = port;
    }

    //----------------------------------------------------------------------------
    int32 IGTConnector::GetPort() const
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      return m_igtClient->ServerPort;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TrackedFrame^ IGTConnector::GetTrackedFrame(double& latestTimestamp)
    {
      auto latestFrame = m_igtClient->GetTrackedFrame(latestTimestamp);
      if (latestFrame == nullptr)
      {
        return nullptr;
      }
      try
      {
        latestTimestamp = latestFrame->Timestamp;
      }
      catch (Platform::ObjectDisposedException^) { return nullptr; }
      return latestFrame;
    }

    //----------------------------------------------------------------------------
    task<void> IGTConnector::KeepAliveAsync()
    {
      m_keepAliveTokenSource = cancellation_token_source();
      auto token = m_keepAliveTokenSource.get_token();
      return create_task([this, token]()
      {
        igtl::StatusMessage::Pointer statusMsg = igtl::StatusMessage::New();
        statusMsg->SetCode(igtl::StatusMessage::STATUS_OK);
        statusMsg->Pack();

        while (!token.is_canceled())
        {
          if (m_connectionState == CONNECTION_STATE_CONNECTED)
          {
            // send keep alive message
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
                  auto connectTask = ConnectAsync(0.1).then([this, &retryCount](task<bool> previousTask)
                  {
                    bool result = previousTask.get();

                    if (!result)
                    {
                      std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_RETRY_DELAY_MSEC));
                      retryCount++;
                    }
                  });

                  try
                  {
                    connectTask.wait();
                  }
                  catch (const std::exception& e)
                  {
                    LOG(LogLevelType::LOG_LEVEL_ERROR, e.what());
                  }
                  catch (Platform::Exception^ e)
                  {
                    WLOG(LogLevelType::LOG_LEVEL_ERROR, e->Message);
                  }
                }

                if (m_connectionState != CONNECTION_STATE_CONNECTED)
                {
                  m_keepAliveTokenSource.cancel();
                  m_keepAliveTokenSource = cancellation_token_source();
                  m_notificationSystem.QueueMessage(L"Connection lost. Check server.");
                  return;
                }
              }
              else
              {
                // Don't reconnect on drop
                m_keepAliveTokenSource.cancel();
                m_keepAliveTokenSource = cancellation_token_source();
                m_notificationSystem.QueueMessage(L"Connection lost. Check server.");
                return;
              }
            }
            else
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(KEEP_ALIVE_INTERVAL_MSEC));
            }
          }
          else
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, "Keep alive running unconnected but token not canceled.");
            std::this_thread::sleep_for(std::chrono::milliseconds(KEEP_ALIVE_INTERVAL_MSEC));
          }
        }
      }, token);
    }
  }
}