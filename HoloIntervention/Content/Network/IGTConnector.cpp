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
    IGTConnector::IGTConnector(System::NotificationSystem& notificationSystem, Input::VoiceInput& input)
      : m_notificationSystem(notificationSystem)
      , m_voiceInput(input)
    {
      /*
      disabled, currently crashes
      FindServersAsync().then([this](task<std::vector<std::wstring>> findServerTask)
      {
        std::vector<std::wstring> servers;
        try
        {
          servers = findServerTask.get();
        }
        catch (const std::exception& e)
        {
          Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("IGTConnector failed to find servers: ") + e.what());
        }
      });
      */

      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    IGTConnector::~IGTConnector()
    {
    }

    //----------------------------------------------------------------------------
    task<bool> IGTConnector::ConnectAsync(double timeoutSec, task_options& options)
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
          Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("IGTConnector failed to connect: ") + e.what());
          return false;
        }

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
    void IGTConnector::Disconnect()
    {
      m_igtClient->Disconnect();
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
    task<std::vector<std::wstring>> IGTConnector::FindServersAsync()
    {
      return create_task([this]()
      {
        std::vector<std::wstring> results;

        auto hostNames = NetworkInformation::GetHostNames();

        for (auto host : hostNames)
        {
          if (host->Type == HostNameType::Ipv4)
          {
            std::wstring hostIP(host->ToString()->Data());
            std::wstring machineIP = hostIP.substr(hostIP.find_last_of(L'.') + 1);
            std::wstring prefix = hostIP.substr(0, hostIP.find_last_of(L'.'));

            // Given a subnet, ping all other IPs
            for (int i = 0; i < 256; ++i)
            {
              std::wstringstream ss;
              ss << i;
              if (ss.str() == machineIP)
              {
                continue;
              }

              UWPOpenIGTLink::IGTLinkClient^ client = ref new UWPOpenIGTLink::IGTLinkClient();
              client->ServerHost = ref new Platform::String((prefix + L"." + ss.str()).c_str());
              client->ServerPort = 18944;

              task<bool> connectTask = create_task(client->ConnectAsync(0.5));
              bool result(false);
              try
              {
                result = connectTask.get();
              }
              catch (const std::exception&)
              {
                continue;
              }

              if (result)
              {
                client->Disconnect();
                results.push_back(prefix + L"." + ss.str());
              }
            }
          }
        }

        return results;
      });
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
    bool IGTConnector::GetTrackedFrame(UWPOpenIGTLink::TrackedFrame^& frame, double* latestTimestamp)
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
    void IGTConnector::RegisterVoiceCallbacks(Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"connect"] = [this](SpeechRecognitionResult ^ result)
      {
        uint64 connectMessageId = m_notificationSystem.QueueMessage(L"Connecting...");
        ConnectAsync(4.0).then([this, connectMessageId](bool result)
        {
          m_notificationSystem.RemoveMessage(connectMessageId);
          if (result)
          {
            m_notificationSystem.QueueMessage(L"Connection successful.");
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Connection failed.");
          }

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
                Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("KeepAliveTask exception: ") + e.what());
              }
            });
          }
        });
      };

      callbackMap[L"set IP"] = [this](SpeechRecognitionResult ^ result)
      {
        m_dictationMatcherToken = m_voiceInput.RegisterDictationMatcher([this](const std::wstring & text)
        {
          bool matchedText(false);

          m_accumulatedDictationResult += text;
          OutputDebugStringW((m_accumulatedDictationResult + L"\n").c_str());

          if (matchedText)
          {
            m_voiceInput.RemoveDictationMatcher(m_dictationMatcherToken);
            m_voiceInput.SwitchToCommandRecognitionAsync();
            return true;
          }
          else
          {
            return false;
          }
        });
        m_voiceInput.SwitchToDictationRecognitionAsync();
        call_after([this]()
        {
          m_voiceInput.RemoveDictationMatcher(m_dictationMatcherToken);
          m_dictationMatcherToken = INVALID_TOKEN;
          m_voiceInput.SwitchToCommandRecognitionAsync();
          m_accumulatedDictationResult.clear();
        }, DICTATION_TIMEOUT_DELAY_MSEC);
      };

      callbackMap[L"disconnect"] = [this](SpeechRecognitionResult ^ result)
      {
        m_keepAliveTokenSource.cancel();

        Disconnect();
        m_notificationSystem.QueueMessage(L"Disconnected.");
      };
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
                    Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e.what());
                  }
                  catch (Platform::Exception^ e)
                  {
                    Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
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
            Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Keep alive running unconnected but token not canceled.");
            std::this_thread::sleep_for(std::chrono::milliseconds(KEEP_ALIVE_INTERVAL_MSEC));
          }
        }
      }, token);
    }
  }
}