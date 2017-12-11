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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// STL includes
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace UI
  {
    class IconEntry;
    class Icons;
  }

  namespace Sound
  {
    class SoundAPI;
  }

  namespace Input
  {
    class VoiceInput : public IEngineComponent
    {
    public:
      VoiceInput(Sound::SoundAPI& soundAPI, UI::Icons& icons);
      ~VoiceInput();

      void EnableVoiceAnalysis(bool enable);
      bool IsVoiceEnabled() const;
      bool IsHearingSound() const;
      bool IsRecognitionActive() const;
      bool IsCommandRecognitionActive() const;
      bool IsDictationRecognitionActive() const;

      Concurrency::task<bool> SwitchToCommandRecognitionAsync();
      Concurrency::task<bool> SwitchToDictationRecognitionAsync();

      Concurrency::task<bool> CompileCallbacksAsync(Input::VoiceInputCallbackMap& callbacks);

      uint64 RegisterDictationMatcher(std::function<bool(const std::wstring& text)> func);
      void RemoveDictationMatcher(uint64 token);

      void Update(DX::StepTimer& timer);

    protected:
      Concurrency::task<bool> SwitchRecognitionAsync(Windows::Media::SpeechRecognition::SpeechRecognizer^ desiredRecognizer);

      void OnResultGenerated(Windows::Media::SpeechRecognition::SpeechContinuousRecognitionSession^ sender, Windows::Media::SpeechRecognition::SpeechContinuousRecognitionResultGeneratedEventArgs^ args);
      void OnHypothesisGenerated(Windows::Media::SpeechRecognition::SpeechRecognizer^ sender, Windows::Media::SpeechRecognition::SpeechRecognitionHypothesisGeneratedEventArgs^ args);
      void OnStateChanged(Windows::Media::SpeechRecognition::SpeechRecognizer^ sender, Windows::Media::SpeechRecognition::SpeechRecognizerStateChangedEventArgs^ args);

      void HandleCommandResult(Windows::Media::SpeechRecognition::SpeechContinuousRecognitionResultGeneratedEventArgs^ args);
      void HandleDictationResult(Windows::Media::SpeechRecognition::SpeechContinuousRecognitionResultGeneratedEventArgs^ args);

      void ProcessMicrophoneLogic(DX::StepTimer& timer);

    protected:
      // Cached entries
      Sound::SoundAPI&                                                  m_soundAPI;
      UI::Icons&                                                        m_icons;

      std::atomic_bool                                                  m_hearingSound = false;
      std::atomic_bool                                                  m_inputEnabled = false;
      std::atomic_bool                                                  m_loadFailed = false;

      // UI variables
      std::shared_ptr<UI::IconEntry>                                    m_iconEntry = nullptr;
      float                                                             m_microphoneBlinkTimer = 0.f;
      bool                                                              m_wasHearingSound = true;
      static const float                                                MICROPHONE_BLINK_TIME_SEC;

      // Voice input variables
      Windows::Media::SpeechRecognition::SpeechRecognizer^              m_activeRecognizer = nullptr;

      Windows::Media::SpeechRecognition::SpeechRecognizer^              m_commandRecognizer = ref new Windows::Media::SpeechRecognition::SpeechRecognizer(Windows::Media::SpeechRecognition::SpeechRecognizer::SystemSpeechLanguage);
      std::unique_ptr<Input::VoiceInputCallbackMap>                     m_callbacks;
      Windows::Foundation::EventRegistrationToken                       m_commandDetectedEventToken;
      Windows::Foundation::EventRegistrationToken                       m_commandStateChangedToken;

      Windows::Media::SpeechRecognition::SpeechRecognizer^              m_dictationRecognizer = ref new Windows::Media::SpeechRecognition::SpeechRecognizer(Windows::Media::SpeechRecognition::SpeechRecognizer::SystemSpeechLanguage);
      uint64                                                            m_nextToken = INVALID_TOKEN;
      std::mutex                                                        m_dictationMatcherMutex;
      std::map<uint64, std::function<bool(const std::wstring& text)>>   m_dictationMatchers;
      Windows::Foundation::EventRegistrationToken                       m_dictationDetectedEventToken;
      Windows::Foundation::EventRegistrationToken                       m_dictationHypothesisGeneratedToken;
      Windows::Foundation::EventRegistrationToken                       m_dictationStateChangedToken;

      const float                                                       MINIMUM_CONFIDENCE_FOR_DETECTION = 0.4f; // [0,1]
    };
  }
}