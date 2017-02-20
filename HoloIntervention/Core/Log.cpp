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
#include "IGTConnector.h"

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
    std::lock_guard<std::mutex> guard(m_sendListMutex);
    m_sendList.push_back(std::pair<LogLevelType, std::wstring>(level, message));
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, Platform::String^ message)
  {
    LogMessage(level, std::wstring(message->Data()));
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

        if (m_sendList.size() == 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        std::pair<LogLevelType, std::wstring> item;
        {
          std::lock_guard<std::mutex> guard(m_sendListMutex);
          item = m_sendList.front();
          m_sendList.pop_front();
        }

        try
        {
          OutputDebugStringW((item.second + L"\n").c_str());
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }
      }
    });
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
}