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
#include "SoundManager.h"

using namespace Concurrency;
using namespace Windows::Foundation;
using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    VoiceInput::VoiceInput()
    {
      m_speechRecognizer = ref new SpeechRecognizer();
      m_speechRecognizer->Constraints->Clear();
    }

    //----------------------------------------------------------------------------
    VoiceInput::~VoiceInput()
    {
      if (m_componentReady)
      {
        m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_speechDetectedEventToken;
        m_speechRecognizer->ContinuousRecognitionSession->StopAsync();
      }
    }

    //----------------------------------------------------------------------------
    void VoiceInput::EnableVoiceAnalysis(bool enable)
    {
      m_speechBeingDetected = enable;
      if (m_componentReady && m_speechBeingDetected)
      {
        m_speechRecognizer->ContinuousRecognitionSession->Resume();
      }
      if (m_componentReady && !m_speechBeingDetected)
      {
        m_speechRecognizer->ContinuousRecognitionSession->PauseAsync();
      }
    }

    //----------------------------------------------------------------------------
    bool VoiceInput::IsVoiceEnabled() const
    {
      return m_speechBeingDetected;
    }

    //----------------------------------------------------------------------------
    task<bool> VoiceInput::CompileCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks)
    {
      Platform::Collections::Vector<Platform::String^ >^ speechCommandList = ref new Platform::Collections::Vector<Platform::String^ >();
      for (auto entry : callbacks)
      {
        speechCommandList->Append(ref new Platform::String(std::get<0>(entry).c_str()));
      }

      SpeechRecognitionListConstraint^ spConstraint = ref new SpeechRecognitionListConstraint(speechCommandList);
      m_speechRecognizer->Constraints->Clear();
      m_speechRecognizer->Constraints->Append(spConstraint);

      return create_task(m_speechRecognizer->CompileConstraintsAsync()).then([this](SpeechRecognitionCompilationResult ^ compilationResult)
      {
        if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
        {
          m_speechDetectedEventToken = m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated +=
                                         ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
                                           std::bind(&VoiceInput::OnResultGenerated, this, std::placeholders::_1, std::placeholders::_2));
          create_task(m_speechRecognizer->ContinuousRecognitionSession->StartAsync()).then([this]()
          {
            m_speechRecognizer->ContinuousRecognitionSession->PauseAsync();
          });
          m_componentReady = true;
        }
        else
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to compile speech patterns.");
        }
      }).then([this, callbacks]()
      {
        if (m_componentReady)
        {
          m_callbacks = callbacks;
          return true;
        }
        else
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Cannot start speech recognition.");
          return false;
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

      if (args->Result->RawConfidence > MINIMUM_CONFIDENCE_FOR_DETECTION)
      {
        HoloIntervention::instance()->GetSoundManager().PlayOmniSoundOnce(L"input_ok");

        // Search the map for the detected command, if matched, call the function
        auto iterator = m_callbacks.find(args->Result->Text->Data());
        if (iterator != m_callbacks.end())
        {
          iterator->second(args->Result);
        }
      }
    }
  }
}