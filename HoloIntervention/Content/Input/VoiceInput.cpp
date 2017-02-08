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
#include "VoiceInput.h"
#include "NotificationSystem.h"

// STL includes
#include <algorithm>

// Windows includes
#include <ppltasks.h>
#include <vccorlib.h>

// Sound includes
#include "SoundAPI.h"

using namespace Concurrency;
using namespace Windows::Foundation;
using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    VoiceInput::VoiceInput(System::NotificationSystem& notificationSystem, Sound::SoundAPI& soundAPI)
      : m_notificationSystem(notificationSystem)
      , m_soundAPI(soundAPI)
      , m_callbacks(std::make_unique<Sound::VoiceInputCallbackMap>())
    {
      // Apply the dictation topic constraint to optimize for dictated freeform speech.
      auto dictationConstraint = ref new SpeechRecognitionTopicConstraint(SpeechRecognitionScenario::Dictation, "dictation");
      m_dictationRecognizer->Constraints->Append(dictationConstraint);

      create_task(m_dictationRecognizer->CompileConstraintsAsync()).then([this](task<SpeechRecognitionCompilationResult^> compilationTask)
      {
        SpeechRecognitionCompilationResult^ compilationResult(nullptr);
        try
        {
          compilationResult = compilationTask.get();
        }
        catch (Platform::Exception^ e)
        {
          OutputDebugStringW(e->Message->Data());
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }

        if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
        {
          m_dictationDetectedEventToken = m_dictationRecognizer->ContinuousRecognitionSession->ResultGenerated +=
                                            ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
                                              std::bind(&VoiceInput::OnResultGenerated, this, std::placeholders::_1, std::placeholders::_2));

          m_dictationHypothesisGeneratedToken = m_dictationRecognizer->HypothesisGenerated += ref new TypedEventHandler<SpeechRecognizer^, SpeechRecognitionHypothesisGeneratedEventArgs^>(
                                                  std::bind(&VoiceInput::OnHypothesisGenerated, this, std::placeholders::_1, std::placeholders::_2));
        }
        else
        {
          m_dictationRecognizer = nullptr;
        }
      });
    }

    //----------------------------------------------------------------------------
    VoiceInput::~VoiceInput()
    {
      if (m_componentReady)
      {
        if (m_activeRecognizer == m_commandRecognizer)
        {
          m_commandRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_commandDetectedEventToken;
          m_commandRecognizer->ContinuousRecognitionSession->StopAsync();
        }
        else if (m_activeRecognizer == m_dictationRecognizer)
        {
          m_dictationRecognizer->HypothesisGenerated -= m_dictationHypothesisGeneratedToken;
          m_dictationRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_dictationDetectedEventToken;
          m_dictationRecognizer->ContinuousRecognitionSession->StopAsync();
        }
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::EnableVoiceAnalysis(bool enable)
    {
      m_speechBeingDetected = enable;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsVoiceEnabled() const
    {
      return m_speechBeingDetected;
    }

    //----------------------------------------------------------------------------
    task<bool> VoiceInput::SwitchToCommandRecognitionAsync()
    {
      return SwitchRecognitionAsync(m_commandRecognizer);
    }

    //----------------------------------------------------------------------------
    task<bool> VoiceInput::SwitchToDictationRecognitionAsync()
    {
      return SwitchRecognitionAsync(m_dictationRecognizer);
    }

    //----------------------------------------------------------------------------
    task<bool> VoiceInput::CompileCallbacksAsync(Sound::VoiceInputCallbackMap& callbacks)
    {
      Platform::Collections::Vector<Platform::String^ >^ speechCommandList = ref new Platform::Collections::Vector<Platform::String^ >();
      for (auto entry : callbacks)
      {
        speechCommandList->Append(ref new Platform::String(std::get<0>(entry).c_str()));
      }

      SpeechRecognitionListConstraint^ listConstraint = ref new SpeechRecognitionListConstraint(speechCommandList);
      m_commandRecognizer->Constraints->Clear();
      m_commandRecognizer->Constraints->Append(listConstraint);

      return create_task(m_commandRecognizer->CompileConstraintsAsync()).then([this](task<SpeechRecognitionCompilationResult^> compilationTask)
      {
        SpeechRecognitionCompilationResult^ compilationResult(nullptr);
        try
        {
          compilationResult = compilationTask.get();
        }
        catch (Platform::Exception^ e)
        {
          OutputDebugStringW(e->Message->Data());
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }

        if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
        {
          m_commandDetectedEventToken = m_commandRecognizer->ContinuousRecognitionSession->ResultGenerated +=
                                          ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
                                            std::bind(&VoiceInput::OnResultGenerated, this, std::placeholders::_1, std::placeholders::_2));

          m_componentReady = true;
        }
        else
        {
          m_notificationSystem.QueueMessage(L"Unable to compile speech patterns.");
        }

        return m_componentReady ? true : false;
      }).then([this, callbacks](bool success)
      {
        if (success)
        {
          *m_callbacks = callbacks;
          return true;
        }
        else
        {
          m_notificationSystem.QueueMessage(L"Cannot start speech recognition.");
          return false;
        }
      });
    }

    //----------------------------------------------------------------------------
    uint64 VoiceInput::RegisterDictationMatcher(std::function<bool(const std::wstring& text)> func)
    {
      std::lock_guard<std::mutex> guard(m_dictationMatcherMutex);
      m_nextToken++;
      m_dictationMatchers[m_nextToken] = func;
      return m_nextToken;
    }

    //----------------------------------------------------------------------------
    void VoiceInput::RemoveDictationMatcher(uint64 token)
    {
      std::lock_guard<std::mutex> guard(m_dictationMatcherMutex);
      auto iter = m_dictationMatchers.find(token);
      if (iter != m_dictationMatchers.end())
      {
        m_dictationMatchers.erase(iter);
      }
      return;
    }

    //----------------------------------------------------------------------------
    task<bool> VoiceInput::SwitchRecognitionAsync(SpeechRecognizer^ desiredRecognizer)
    {
      return create_task([this, desiredRecognizer]()
      {
        if (m_activeRecognizer == desiredRecognizer)
        {
          return task_from_result(true);
        }

        if (m_activeRecognizer != nullptr)
        {
          return create_task(m_activeRecognizer->ContinuousRecognitionSession->StopAsync()).then([this, desiredRecognizer](task<void> stopTask) -> task<bool>
          {
            try
            {
              stopTask.wait();
              m_activeRecognizer = nullptr;
            }
            catch (const std::exception& e)
            {
              Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("Failed to stop current recognizer: ") + e.what());
              return task_from_result(false);
            }

            if (desiredRecognizer == nullptr)
            {
              return task_from_result(true);
            }

            return create_task(desiredRecognizer->ContinuousRecognitionSession->StartAsync()).then([this, desiredRecognizer](task<void> startTask)
            {
              try
              {
                startTask.wait();
                m_activeRecognizer = desiredRecognizer;
              }
              catch (const std::exception& e)
              {
                Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("Failed to start desired recognizer: ") + e.what());
                return false;
              }

              return true;
            });
          });
        }
        else
        {
          if (desiredRecognizer == nullptr)
          {
            return task_from_result(true);
          }

          return create_task(desiredRecognizer->ContinuousRecognitionSession->StartAsync()).then([this, desiredRecognizer](task<void> startTask)
          {
            try
            {
              startTask.wait();
              m_activeRecognizer = desiredRecognizer;
            }
            catch (const std::exception& e)
            {
              Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("Failed to start command recognizer: ") + e.what());
              return false;
            }

            return true;
          });
        }
      });
    }

    //----------------------------------------------------------------------------
    void VoiceInput::OnResultGenerated(SpeechContinuousRecognitionSession^ sender, SpeechContinuousRecognitionResultGeneratedEventArgs^ args)
    {
      if (!m_speechBeingDetected)
      {
        return;
      }

      if (m_activeRecognizer == m_commandRecognizer)
      {
        HandleCommandResult(args);
      }
      else if (m_activeRecognizer == m_dictationRecognizer)
      {
        HandleDictationResult(args);
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::OnHypothesisGenerated(SpeechRecognizer^ sender, SpeechRecognitionHypothesisGeneratedEventArgs^ args)
    {
      if (!m_speechBeingDetected)
      {
        return;
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::HandleCommandResult(SpeechContinuousRecognitionResultGeneratedEventArgs^ args)
    {
      if (args->Result->RawConfidence > MINIMUM_CONFIDENCE_FOR_DETECTION)
      {
        // Search the map for the detected command, if matched, call the function
        auto iterator = m_callbacks->find(args->Result->Text->Data());
        if (iterator != m_callbacks->end())
        {
          m_soundAPI.PlayOmniSoundOnce(L"input_ok");
          iterator->second(args->Result);
        }
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::HandleDictationResult(SpeechContinuousRecognitionResultGeneratedEventArgs^ args)
    {
      for (auto pair : m_dictationMatchers)
      {
        pair.second(std::wstring(args->Result->Text->Data()));
      }
    }
  }
}