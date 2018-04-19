/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Log.h"

// System includes
#include "NotificationSystem.h"

using namespace Concurrency;
using namespace Windows::Networking;
using namespace Windows::Storage;
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
    DataWriterAsync();
  }

  //----------------------------------------------------------------------------
  Log::~Log()
  {
    m_tokenSource.cancel();
    std::lock_guard<std::mutex> guard(m_writerMutex);
    m_logWriter->FlushAsync();
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::string& message, const std::string& file, int32 line)
  {
    LogMessage(level, std::wstring(begin(message), end(message)), std::wstring(begin(file), end(file)), line);
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, const std::wstring& message, const std::wstring& file, int32 line)
  {
    MessageEntry msgEntry;
    msgEntry.Level = level;
    msgEntry.Message = message;
    msgEntry.File = file;
    msgEntry.Line = line;

    std::lock_guard<std::mutex> guard(m_messagesMutex);
    m_messages.push_back(msgEntry);
  }

  //----------------------------------------------------------------------------
  void Log::LogMessage(LogLevelType level, Platform::String^ message, Platform::String^ file, int32 line)
  {
    LogMessage(level, message->Data(), file->Data(), line);
  }

  //----------------------------------------------------------------------------
  Concurrency::task<void> Log::EndSessionAsync()
  {
    m_tokenSource.cancel();
    std::lock_guard<std::mutex> guard(m_writerMutex);
    return create_task(m_logWriter->StoreAsync()).then([this](uint32 bytes)
    {
      return create_task(m_logStream->FlushAsync()).then([this](bool result)
      {
        std::lock_guard<std::mutex> guard(m_writerMutex);
        m_logWriter->DetachStream();
        m_logWriter = nullptr;
        m_logFile = nullptr;
        m_tokenSource = cancellation_token_source();

        return DataWriterAsync();
      });
    });
  }

  //----------------------------------------------------------------------------
  void Log::SetLogLevel(LogLevelType level)
  {
    m_logLevel = level;
  }

  //----------------------------------------------------------------------------
  std::wstring Log::LogLevelToWString(LogLevelType level)
  {
    switch (level)
    {
    case HoloIntervention::LogLevelType::LOG_LEVEL_ERROR:
      return L"LOG_LEVEL_ERROR";
    case HoloIntervention::LogLevelType::LOG_LEVEL_WARNING:
      return L"LOG_LEVEL_WARNING";
    case HoloIntervention::LogLevelType::LOG_LEVEL_INFO:
      return L"LOG_LEVEL_INFO";
    case HoloIntervention::LogLevelType::LOG_LEVEL_DEBUG:
      return L"LOG_LEVEL_DEBUG";
    case HoloIntervention::LogLevelType::LOG_LEVEL_TRACE:
      return L"LOG_LEVEL_TRACE";
    }

    return L"LOG_LEVEL_UNKNOWN";
  }

  //----------------------------------------------------------------------------
  std::string Log::LogLevelToString(LogLevelType level)
  {
    switch (level)
    {
    case HoloIntervention::LogLevelType::LOG_LEVEL_ERROR:
      return "LOG_LEVEL_ERROR";
    case HoloIntervention::LogLevelType::LOG_LEVEL_WARNING:
      return "LOG_LEVEL_WARNING";
    case HoloIntervention::LogLevelType::LOG_LEVEL_INFO:
      return "LOG_LEVEL_INFO";
    case HoloIntervention::LogLevelType::LOG_LEVEL_DEBUG:
      return "LOG_LEVEL_DEBUG";
    case HoloIntervention::LogLevelType::LOG_LEVEL_TRACE:
      return "LOG_LEVEL_TRACE";
    }

    return "LOG_LEVEL_UNKNOWN";
  }

  //----------------------------------------------------------------------------
  HoloIntervention::LogLevelType Log::StringToLogLevel(const std::string& level)
  {
    if (IsEqualInsensitive("LOG_LEVEL_INFO", level))
    {
      return LogLevelType::LOG_LEVEL_INFO;
    }
    else if (IsEqualInsensitive("LOG_LEVEL_ERROR", level))
    {
      return LogLevelType::LOG_LEVEL_ERROR;
    }
    else if (IsEqualInsensitive("LOG_LEVEL_WARNING", level))
    {
      return LogLevelType::LOG_LEVEL_WARNING;
    }
    else if (IsEqualInsensitive("LOG_LEVEL_DEBUG", level))
    {
      return LogLevelType::LOG_LEVEL_DEBUG;
    }
    else if (IsEqualInsensitive("LOG_LEVEL_TRACE", level))
    {
      return LogLevelType::LOG_LEVEL_TRACE;
    }
    else
    {
      return LogLevelType::LOG_LEVEL_UNKNOWN;
    }
  }

  //----------------------------------------------------------------------------
  HoloIntervention::LogLevelType Log::WStringToLogLevel(const std::wstring& level)
  {
    if (IsEqualInsensitive(L"LOG_LEVEL_INFO", level))
    {
      return LogLevelType::LOG_LEVEL_INFO;
    }
    else if (IsEqualInsensitive(L"LOG_LEVEL_ERROR", level))
    {
      return LogLevelType::LOG_LEVEL_ERROR;
    }
    else if (IsEqualInsensitive(L"LOG_LEVEL_WARNING", level))
    {
      return LogLevelType::LOG_LEVEL_WARNING;
    }
    else if (IsEqualInsensitive(L"LOG_LEVEL_DEBUG", level))
    {
      return LogLevelType::LOG_LEVEL_DEBUG;
    }
    else if (IsEqualInsensitive(L"LOG_LEVEL_TRACE", level))
    {
      return LogLevelType::LOG_LEVEL_TRACE;
    }
    else
    {
      return LogLevelType::LOG_LEVEL_UNKNOWN;
    }
  }

  //----------------------------------------------------------------------------
  task<void> Log::DataWriterAsync()
  {
    return create_task([this, token = m_tokenSource.get_token()]() -> void
    {
      auto userFolder = ApplicationData::Current->LocalFolder;
      auto Calendar = ref new Windows::Globalization::Calendar();
      Calendar->SetToNow();
      auto fileName = L"HoloIntervention_" + Calendar->YearAsString() + L"-" + Calendar->MonthAsNumericString() + L"-" + Calendar->DayAsString() + L"T" + Calendar->HourAsPaddedString(2) + L"h" + Calendar->MinuteAsPaddedString(2) + L"m" + Calendar->SecondAsPaddedString(2) + L"s.txt";

      std::atomic_bool fileReady(false);
      create_task(userFolder->CreateFileAsync(fileName, CreationCollisionOption::GenerateUniqueName)).then([this, &fileReady](StorageFile ^ file)
      {
        m_logFile = file;
        return create_task(m_logFile->OpenAsync(FileAccessMode::ReadWrite)).then([this, &fileReady](IRandomAccessStream ^ stream)
        {
          m_logStream = stream;
          std::lock_guard<std::mutex> guard(m_writerMutex);
          m_logWriter = ref new DataWriter(m_logStream->GetOutputStreamAt(0));
          fileReady = true;
        });
      });

      if (!wait_until_condition([&fileReady]() {bool x = fileReady; return x; }, 5000))
      {
        OutputDebugStringA("Cannot create log file. No logging possible.");
        return;
      }

      PeriodicFlushAsync();

      while (!token.is_canceled())
      {
        std::atomic_bool wroteMessage(false);
        std::unique_lock<std::mutex> guard(m_messagesMutex);
        while (m_messages.size() > 0)
        {
          MessageEntry item;
          {
            item = m_messages.front();
            m_messages.pop_front();
          }

          // Only write message if item's level is above/equal current logging level
          if (item.Level >= m_logLevel)
          {
            try
            {
              std::lock_guard<std::mutex> guard(m_writerMutex);
              auto output = item.Level.ToString() + L"|" + ref new Platform::String(item.Message.c_str()) + L"|" + ref new Platform::String(item.File.c_str()) + L":" + item.Line.ToString() + L"\n";
              m_logWriter->WriteString(output);
              wroteMessage = true;
            }
            catch (Platform::Exception^) { try { m_logWriter->FlushAsync(); } catch (Platform::Exception^) { return; } return; }
          }
        }

        if (wroteMessage)
        {
          m_logWriter->StoreAsync();
        }

        guard.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
    });
  }

  //----------------------------------------------------------------------------
  Concurrency::task<void> Log::PeriodicFlushAsync()
  {
    return create_task([this, token = m_tokenSource.get_token()]()
    {
      while (!token.is_canceled())
      {
        uint32 accumulator = 0;
        while (accumulator < FLUSH_PERIOD_MSEC)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          accumulator += 100;
        }
        std::lock_guard<std::mutex> guard(m_writerMutex);
        m_logStream->FlushAsync();
      }
    });
  }
}