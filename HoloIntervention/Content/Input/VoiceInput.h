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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// STL includes
#include <functional>
#include <map>
#include <string>

namespace HoloIntervention
{
  namespace Sound
  {
    class SoundAPI;
  }

  namespace Input
  {
    class VoiceInput : public IEngineComponent
    {
    public:
      VoiceInput(System::NotificationSystem& notificationSystem, Sound::SoundAPI& soundAPI);
      ~VoiceInput();

      void EnableVoiceAnalysis(bool enable);
      bool IsVoiceEnabled() const;

      Concurrency::task<bool> CompileCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks);

    protected:
      void OnResultGenerated(Windows::Media::SpeechRecognition::SpeechContinuousRecognitionSession^ sender, Windows::Media::SpeechRecognition::SpeechContinuousRecognitionResultGeneratedEventArgs^ args);

    protected:
      // Cached entries
      System::NotificationSystem&                           m_notificationSystem;
      Sound::SoundAPI&                                      m_soundAPI;

      std::atomic_bool                                      m_speechBeingDetected = false;
      Windows::Media::SpeechRecognition::SpeechRecognizer^  m_speechRecognizer = nullptr;
      HoloIntervention::Sound::VoiceInputCallbackMap        m_callbacks;
      Windows::Foundation::EventRegistrationToken           m_speechDetectedEventToken;

      const float                                           MINIMUM_CONFIDENCE_FOR_DETECTION = 0.4f; // [0,1]
    };
  }
}