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
#include "Log.h"

// System includes
#include "NotificationSystem.h"

// Network includes
#include "IGTConnector.h"

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
    DataSenderAsync();
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
  task<void> Log::SuspendAsync()
  {
    m_tokenSource.cancel();
    return create_task(m_logWriter->FlushAsync()).then([this](bool result)
    {
      m_logWriter = nullptr;
      m_logFile = nullptr;
    });
  }

  //----------------------------------------------------------------------------
  task<void> Log::ResumeAsync()
  {
    DataSenderAsync();
    return create_task([]() {});
  }

  //----------------------------------------------------------------------------
  task<void> Log::DataSenderAsync()
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
          std::lock_guard<std::mutex> guard(m_writerMutex);
          m_logWriter = ref new DataWriter(stream);
          fileReady = true;
        });
      });

      if (!wait_until_condition([&fileReady]() {bool x = fileReady; return x; }, 5000))
      {
        OutputDebugStringA("Cannot create log file. No logging possible.");
        return;
      }

      PeriodicFlushAsync();

      while (true)
      {
        if (token.is_canceled())
        {
          return;
        }

        if (m_messages.size() == 0)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(25));
          continue;
        }

        MessageEntry item;
        {
          std::lock_guard<std::mutex> guard(m_messagesMutex);
          item = m_messages.front();
          m_messages.pop_front();
        }

        try
        {
          std::lock_guard<std::mutex> guard(m_writerMutex);
          auto output = L"|" + item.Level.ToString() + L"|" + ref new Platform::String(item.Message.c_str()) + L"|" + ref new Platform::String(item.File.c_str()) + L":" + item.Line.ToString();
          m_logWriter->WriteString(output);
        }
        catch (Platform::Exception^) { try { m_logWriter->FlushAsync(); } catch (Platform::Exception^) { return; } return; }
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
        m_logWriter->FlushAsync();
      }
    });
  }
}