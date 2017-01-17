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
  Log::Log()
  {
  }

  //----------------------------------------------------------------------------
  Log::~Log()
  {
    if (m_connected)
    {
      m_socket->Close();
      m_connected = false;
    }
  }


  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::string& message)
  {
    LogMessage(level, std::wstring(message.begin(), message.end()));
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::wstring& message)
  {
    SendMessageAsync(level, message).then([this, message](task<bool> previousTask)
    {
      bool result(false);
      try
      {
        result = previousTask.get();
      }
      catch (const std::exception& e)
      {
        OutputDebugStringA(e.what());
      }

      if (!result)
      {
        OutputDebugStringW(message.c_str());
      }
    });
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
  Concurrency::task<void> Log::DataReceiverAsync()
  {
    auto token = m_tokenSource.get_token();
    return create_task([ = ]()
    {
      DataReader^ reader = ref new DataReader(m_socket->InputStream);
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
      }
    }, m_tokenSource.get_token());
  }

  //----------------------------------------------------------------------------
  task<bool> Log::SendMessageAsync(LogLevelType level, const std::wstring& message)
  {
    return create_task([ = ]() -> bool
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

        m_socket->Control->KeepAlive = true;
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

        m_connected = true;
      }

      DataWriter^ writer = ref new DataWriter(m_socket->OutputStream);

      writer->WriteString(level.ToString() + L"||" + ref new Platform::String(message.c_str()));

      auto storeTask = create_task(writer->StoreAsync()).then([ = ](task<uint32> writeTask)
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

        if (bytesWritten != message.length())
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
      }

      if (bytesWritten > 0)
      {
        return true;
      }
      return false;
    }, task_continuation_context::use_arbitrary());
  }
}