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
#include "AppView.h"
#include "Common.h"
#include "VoiceInput.h"

// UI includes
#include "Icons.h"

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
    const float VoiceInput::MICROPHONE_BLINK_TIME_SEC = 1.f;

    //----------------------------------------------------------------------------
    VoiceInput::VoiceInput(Sound::SoundAPI& soundAPI, UI::Icons& icons)
      : m_soundAPI(soundAPI)
      , m_icons(icons)
      , m_callbacks(std::make_unique<Input::VoiceInputCallbackMap>())
    {
      // Apply the dictation topic constraint to optimize for dictated freeform speech.
      auto dictationConstraint = ref new SpeechRecognitionTopicConstraint(SpeechRecognitionScenario::Dictation, "dictation");
      m_dictationRecognizer->Constraints->Append(dictationConstraint);

      m_icons.AddEntryAsync(L"Assets/Models/microphone_icon.cmo", 0).then([this](std::shared_ptr<UI::Icon> entry)
      {
        m_microphoneIcon = entry;
      });

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

          m_dictationStateChangedToken = m_dictationRecognizer->StateChanged +=
                                           ref new TypedEventHandler<SpeechRecognizer^, SpeechRecognizerStateChangedEventArgs^>(
                                             std::bind(&VoiceInput::OnStateChanged, this, std::placeholders::_1, std::placeholders::_2));
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
      m_commandRecognizer->StateChanged -= m_commandStateChangedToken;
      m_commandRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_commandDetectedEventToken;

      m_dictationRecognizer->StateChanged -= m_dictationStateChangedToken;
      m_dictationRecognizer->HypothesisGenerated -= m_dictationHypothesisGeneratedToken;
      m_dictationRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_dictationDetectedEventToken;

      if (m_activeRecognizer == nullptr)
      {
        return;
      }

      if (m_activeRecognizer == m_commandRecognizer)
      {
        m_commandRecognizer->ContinuousRecognitionSession->StopAsync();
      }
      else if (m_activeRecognizer == m_dictationRecognizer)
      {
        m_dictationRecognizer->ContinuousRecognitionSession->StopAsync();
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::EnableVoiceAnalysis(bool enable)
    {
      m_inputEnabled = enable;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsVoiceEnabled() const
    {
      return m_inputEnabled;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsHearingSound() const
    {
      return m_hearingSound;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsRecognitionActive() const
    {
      return m_activeRecognizer != nullptr && m_activeRecognizer->State != SpeechRecognizerState::Paused && m_activeRecognizer->State != SpeechRecognizerState::Paused;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsCommandRecognitionActive() const
    {
      return m_activeRecognizer == m_commandRecognizer;
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsDictationRecognitionActive() const
    {
      return m_activeRecognizer == m_dictationRecognizer;
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
    task<bool> VoiceInput::CompileCallbacksAsync(Input::VoiceInputCallbackMap& callbacks)
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

        if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
        {
          m_commandDetectedEventToken = m_commandRecognizer->ContinuousRecognitionSession->ResultGenerated +=
                                          ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
                                            std::bind(&VoiceInput::OnResultGenerated, this, std::placeholders::_1, std::placeholders::_2));

          m_commandStateChangedToken = m_commandRecognizer->StateChanged +=
                                         ref new TypedEventHandler<SpeechRecognizer^, SpeechRecognizerStateChangedEventArgs^>(
                                           std::bind(&VoiceInput::OnStateChanged, this, std::placeholders::_1, std::placeholders::_2));


          m_componentReady = true;
        }
        else
        {
          LOG_ERROR(L"Unable to compile speech patterns.");
          m_loadFailed = true;
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
          LOG_ERROR(L"Cannot start speech recognition.");
          m_loadFailed = true;
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
    void VoiceInput::Update(DX::StepTimer& timer)
    {
      ProcessMicrophoneLogic(timer);
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
            catch (Platform::Exception^ e)
            {
              LOG_ERROR(L"Failed to stop current recognizer: " + e->Message);
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
              catch (Platform::Exception^ e)
              {
                LOG_ERROR(L"Failed to start desired recognizer: " + e->Message);
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
            catch (Platform::Exception^ e)
            {
              LOG_ERROR(L"Failed to start command recognizer: " + e->Message);
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
      if (!m_inputEnabled)
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
      if (!m_inputEnabled)
      {
        return;
      }
      OutputDebugStringW(L"hypothesis");
    }

    //----------------------------------------------------------------------------
    void VoiceInput::OnStateChanged(SpeechRecognizer^ sender, SpeechRecognizerStateChangedEventArgs^ args)
    {
      if (args->State == SpeechRecognizerState::SoundStarted)
      {
        m_hearingSound = true;
      }
      else if (args->State == SpeechRecognizerState::SoundEnded)
      {
        m_hearingSound = false;
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

    //----------------------------------------------------------------------------
    void VoiceInput::ProcessMicrophoneLogic(DX::StepTimer& timer)
    {
      if (m_microphoneIcon == nullptr || !m_microphoneIcon->GetModel()->IsLoaded())
      {
        return;
      }

      if (m_loadFailed)
      {
        m_microphoneIcon->GetModel()->SetVisible(true);
        m_microphoneIcon->GetModel()->SetColour(1.f, 0.f, 0.f, 1.f);
        return;
      }

      if (!m_wasHearingSound && IsHearingSound())
      {
        // Colour!
        m_wasHearingSound = true;
        m_microphoneIcon->GetModel()->SetVisible(true);
        m_microphoneIcon->GetModel()->SetRenderingState(Rendering::RENDERING_DEFAULT);
      }
      else if (m_wasHearingSound && !IsHearingSound())
      {
        // Greyscale
        m_wasHearingSound = false;
        m_microphoneIcon->GetModel()->SetVisible(true);
        m_microphoneIcon->GetModel()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
      }
      else if (m_wasHearingSound && IsHearingSound())
      {
        // Blink!
        m_microphoneBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
        if (m_microphoneBlinkTimer >= MICROPHONE_BLINK_TIME_SEC)
        {
          m_microphoneBlinkTimer = 0.f;
          m_microphoneIcon->GetModel()->ToggleVisible();
        }
      }
    }
  }
}