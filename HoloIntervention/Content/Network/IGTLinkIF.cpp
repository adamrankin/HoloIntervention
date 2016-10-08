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

namespace HoloIntervention
{
  namespace Network
  {
    const double IGTLinkIF::CONNECT_TIMEOUT_SEC = 3.0;

    //----------------------------------------------------------------------------
    IGTLinkIF::IGTLinkIF()
    {
    }

    //----------------------------------------------------------------------------
    IGTLinkIF::~IGTLinkIF()
    {
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> IGTLinkIF::ConnectAsync(double timeoutSec)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      auto connectTask = create_task(m_igtClient->ConnectAsync(timeoutSec));

      connectTask.then([this](bool result)
      {
        m_connected = result;
      });

      return connectTask;
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::Disconnect()
    {
      m_connected = false;
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->Disconnect();
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::IsConnected()
    {
      return m_connected;
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
      return std::wstring(m_igtClient->ServerHost->Data());
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::SetPort(int32 port)
    {
      std::lock_guard<std::mutex> guard(m_clientMutex);
      m_igtClient->ServerPort = port;
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::GetLatestTrackedFrame(UWPOpenIGTLink::TrackedFrame^ frame, double* latestTimestamp)
    {
      return m_igtClient->GetLatestTrackedFrame(frame, latestTimestamp);
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::GetLatestCommand(UWPOpenIGTLink::Command^ cmd, double* latestTimestamp)
    {
      return m_igtClient->GetLatestCommand(cmd, latestTimestamp);
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg)
    {
      callbackMap[L"connect"] = [this](SpeechRecognitionResult ^ result)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Connecting...");
        this->ConnectAsync(4.0).then([this](bool result)
        {
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
            try
            {
              m_keepAliveTask->wait();
            }
            catch (const std::exception& e)
            {
              OutputDebugStringA(e.what());
              m_keepAliveTask = nullptr;
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(e->Message->Data());
              m_keepAliveTask = nullptr;
            }
          }
        });
      };

      callbackMap[L"disconnect"] = [this](SpeechRecognitionResult ^ result)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Disconnected.");
        this->Disconnect();
      };
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte> IGTLinkIF::GetSharedImagePtr(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      return *(std::shared_ptr<byte>*)frame->ImageDataSharedPtr;
    }

    //----------------------------------------------------------------------------
    Concurrency::task<void> IGTLinkIF::KeepAliveAsync()
    {
      m_tokenSource = cancellation_token_source();
      auto token = m_tokenSource.get_token();
      return create_task([this, token]()
      {
        while (!token.is_canceled())
        {
          if (m_connected)
          {
            // send keep alive message
            std::lock_guard<std::mutex> guard(m_clientMutex);
            igtl::StatusMessage::Pointer statusMsg = igtl::StatusMessage::New();
            statusMsg->SetCode(igtl::StatusMessage::STATUS_OK);
            statusMsg->Pack();
            bool result(false);
            RETRY_UNTIL_TRUE(result = m_igtClient->SendMessage((UWPOpenIGTLink::MessageBasePointerPtr)&statusMsg), 10, 25);
            if (!result)
            {
              m_tokenSource.cancel();
              m_connected = false;
              m_igtClient->Disconnect();
              if (m_reconnectOnDrop)
              {

              }
            }
          }
          else
          {
            OutputDebugStringA("Keep alive running unconnected but token not canceled.");
          }
        }
      }, m_tokenSource.get_token());
    }
  }
}