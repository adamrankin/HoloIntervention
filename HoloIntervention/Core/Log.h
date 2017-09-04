#pragma once
/*====================================================================
Copyright(c) 2017 Adam Rankin


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

// STL includes
#include <deque>

namespace HoloIntervention
{
  namespace Network
  {
    class IGTConnector;
  }

  public enum class LogLevelType : int32
  {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
  };

  class Log : public IEngineComponent
  {
    struct MessageEntry
    {
      LogLevelType  Level;
      std::wstring  Message;
      std::wstring  File;
      int32         Line;
    };

  public:
    static Log& instance();

    void LogMessage(LogLevelType level, Platform::String^ message, Platform::String^ file, int32 line);
    void LogMessage(LogLevelType level, const std::string& message, const std::string& file, int32 line);
    void LogMessage(LogLevelType level, const std::wstring& message, const std::wstring& file, int32 line);

    Concurrency::task<void> EndSessionAsync();

  protected:
    Concurrency::task<void> DataWriterAsync();
    Concurrency::task<void> PeriodicFlushAsync();

  protected:
    Log();
    ~Log();

  protected:
    Concurrency::cancellation_token_source          m_tokenSource;
    std::atomic_bool                                m_writerActive = false;

    std::mutex                                      m_writerMutex;
    Windows::Storage::StorageFile^                  m_logFile;
    Windows::Storage::Streams::IRandomAccessStream^ m_logStream;
    Windows::Storage::Streams::DataWriter^          m_logWriter;

    std::mutex                                      m_messagesMutex;
    std::deque<MessageEntry>                        m_messages;

    const uint32                                    FLUSH_PERIOD_MSEC = 2000;
  };
}