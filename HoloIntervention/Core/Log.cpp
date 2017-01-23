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
#include "Log.h"

// System includes
#include "NotificationSystem.h"

// Network includes
#include "IGTLinkIF.h"

using namespace Concurrency;
using namespace Windows::Networking;
using namespace Windows::Storage::Streams;

namespace HoloIntervention
{

  //----------------------------------------------------------------------------
  HoloIntervention::Log& Log::instance()
  {
    static Log instance;
    return instance;
  }

  //----------------------------------------------------------------------------
  Log::Log()
  {
    DataSenderAsync();
  }

  //----------------------------------------------------------------------------
  Log::~Log()
  {
    m_tokenSource.cancel();
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::string& message)
  {
    LogMessage(level, std::wstring(message.begin(), message.end()));
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::wstring& message)
  {
    m_sendList.push_back(std::pair<LogLevelType, std::wstring>(level, message));
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, Platform::String^ message)
  {
    LogMessage(level, std::wstring(message->Data()));
  }

  //----------------------------------------------------------------------------
  void Log::SetPort(uint32 port)
  {
    m_port = port;
  }

  //----------------------------------------------------------------------------
  uint32 Log::GetPort() const
  {
    return m_port;
  }

  //----------------------------------------------------------------------------
  void Log::SetHostname(const std::wstring& hostname)
  {
    m_hostname = ref new Platform::String(hostname.c_str());
  }

  //----------------------------------------------------------------------------
  std::wstring Log::GetHostname() const
  {
    return std::wstring(m_hostname->Data());
  }

  //----------------------------------------------------------------------------
  task<void> Log::DataSenderAsync()
  {
    auto token = m_tokenSource.get_token();
    return create_task([this, token]()
    {
      while (true)
      {
        if (token.is_canceled())
        {
          return;
        }

        auto connectionTask = ConnectAsync();
        bool result = connectionTask.get();

        if (!result || m_sendList.size() == 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        std::pair<LogLevelType, std::wstring> item = m_sendList.front();
        m_sendList.pop_front();
        try
        {
          SendMessageAsync(item.first, item.second).then([this, item](bool result)
          {
            if (!result)
            {
              m_sendList.push_front(item);
            }
          });
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
          m_reader = nullptr;
          m_writer = nullptr;
          m_connected = false;
        }
      }
    });

    delete m_socket;
    m_connected = false;
  }

  //----------------------------------------------------------------------------
  task<void> Log::DataReceiverAsync()
  {
    auto token = m_tokenSource.get_token();
    return create_task([this, token]()
    {
      while (true)
      {
        if (token.is_canceled())
        {
          return;
        }

        // TODO : any data reading need?
        /*
        auto readTask = create_task(reader->LoadAsync(byteSize));
        int bytesRead(-1);
        try
        {
          bytesRead = readTask.get();
        }
        catch (Platform::Exception^ e)
        {
          OutputDebugStringW(e->Message->Data());
        }

        if (bytesRead != byteSize)
        {
          throw std::exception("Bad read over network.");
        }

        IBuffer^ buffer = reader->ReadBuffer(byteSize);
        */

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    });
  }

  //----------------------------------------------------------------------------
  task<bool> Log::SendMessageAsync(LogLevelType level, const std::wstring& message)
  {
    return create_task([ = ]() -> bool
    {
      std::wstring msg = LevelToString(level) + L"||" + message;
      std::string msgStr(msg.begin(), msg.end());

      m_writer->WriteUInt32(msgStr.length());
      Platform::Array<byte>^ data = ref new Platform::Array<byte>((byte*)&msgStr[0], msg.length());
      m_writer->WriteBytes(data);

      auto storeTask = create_task(m_writer->StoreAsync()).then([ = ](task<uint32> writeTask)
      {
        uint32 bytesWritten;
        try
        {
          bytesWritten = writeTask.get();
        }
        catch (Platform::Exception^ exception)
        {
          std::wstring message(exception->Message->Data());
          std::string messageStr(message.begin(), message.end());
          throw std::exception(messageStr.c_str());
        }

        if (bytesWritten != sizeof(uint32) + msg.length())
        {
          throw std::exception("Entire message couldn't be sent.");
        }

        return bytesWritten;
      });

      int bytesWritten;
      try
      {
        bytesWritten = storeTask.get();
      }
      catch (const std::exception& e)
      {
        OutputDebugStringA(e.what());
        return false;
      }

      if (bytesWritten > 0)
      {
        return true;
      }
      return false;
    }, task_continuation_context::use_arbitrary());
  }

  //----------------------------------------------------------------------------
  std::wstring Log::LevelToString(LogLevelType type)
  {
    switch (type)
    {
      case HoloIntervention::Log::LOG_LEVEL_ERROR:
        return L"ERROR";
        break;
      case HoloIntervention::Log::LOG_LEVEL_WARNING:
        return L"WARNING";
        break;
      case HoloIntervention::Log::LOG_LEVEL_INFO:
        return L"INFO";
        break;
      case HoloIntervention::Log::LOG_LEVEL_DEBUG:
        return L"DEBUG";
        break;
      case HoloIntervention::Log::LOG_LEVEL_TRACE:
        return L"TRACE";
        break;
      default:
        return L"UNKNOWN";
        break;
    }
  }

  //----------------------------------------------------------------------------
  task<bool> Log::ConnectAsync()
  {
    return create_task([this]()
    {
      if (!m_connected)
      {
        Windows::Networking::HostName^ hostname(nullptr);
        if (m_hostname == nullptr)
        {
          hostname = ref new HostName(ref new Platform::String(HoloIntervention::instance()->GetIGTLink().GetHostname().c_str()));
        }
        else
        {
          hostname = ref new HostName(m_hostname);
        }

        auto connectTask = create_task(m_socket->ConnectAsync(hostname, m_port.ToString()));

        try
        {
          connectTask.wait();
        }
        catch (Platform::Exception^ e)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to connect to log server.");
          return false;
        }

        m_reader = ref new DataReader(m_socket->InputStream);
        m_writer = ref new DataWriter(m_socket->OutputStream);
        m_connected = true;
      }

      return true;
    });
  }
}