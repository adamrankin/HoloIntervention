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

// Local includes
#include "IEngineComponent.h"

// WinRT includes
#include <ppltasks.h>

namespace HoloIntervention
{
  class Log : public IEngineComponent
  {
  public:
    enum LogLevelType
    {
      LOG_LEVEL_ERROR = 1,
      LOG_LEVEL_WARNING = 2,
      LOG_LEVEL_INFO = 3,
      LOG_LEVEL_DEBUG = 4,
      LOG_LEVEL_TRACE = 5,

      LOG_LEVEL_DEFAULT = LOG_LEVEL_INFO,
    };
    // TODO : make singleton
  public:
    Log();
    ~Log();

    void LogMessage(LogLevelType level, Platform::String^ message);
    void LogMessage(LogLevelType level, const std::string& message);
    void LogMessage(LogLevelType level, const std::wstring& message);

    void SetPort(uint32 port);
    uint32 GetPort() const;

    void SetHostname(const std::wstring& hostname);
    std::wstring GetHostname()const;;

  protected:
    Concurrency::task<void> DataReceiverAsync();
    Concurrency::task<bool> SendMessageAsync(LogLevelType level, const std::wstring& message);

    Concurrency::cancellation_token_source          m_tokenSource;
    Concurrency::task<void>*                        m_receiverTask = nullptr;
    Windows::Networking::Sockets::StreamSocket^     m_socket = ref new Windows::Networking::Sockets::StreamSocket();
    std::atomic_bool                                m_connected = false;

    Platform::String^                               m_hostname = nullptr;
    uint32                                          m_port = 24012;
  };
}