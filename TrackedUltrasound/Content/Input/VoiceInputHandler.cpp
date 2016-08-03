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
#include "VoiceInputHandler.h"
#include "NotificationsAPI.h"

// Windows includes
#include <ppltasks.h>
#include <vccorlib.h>

using namespace Concurrency;
using namespace Windows::Foundation;
using namespace Windows::Media::SpeechRecognition;

namespace TrackedUltrasound
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    VoiceInputHandler::VoiceInputHandler()
      : m_speechRecognizer(ref new SpeechRecognizer())
    {
      Platform::Collections::Vector<Platform::String^>^ speechCommandList = ref new Platform::Collections::Vector<Platform::String^>();

      speechCommandList->Append(Platform::StringReference(L"show"));
      speechCommandList->Append(Platform::StringReference(L"hide"));
      speechCommandList->Append(Platform::StringReference(L"connect"));
      speechCommandList->Append(Platform::StringReference(L"disconnect"));

      SpeechRecognitionListConstraint^ spConstraint = ref new SpeechRecognitionListConstraint(speechCommandList);
      m_speechRecognizer->Constraints->Clear();
      m_speechRecognizer->Constraints->Append(spConstraint);

      create_task(m_speechRecognizer->CompileConstraintsAsync()).then([this](SpeechRecognitionCompilationResult^ compilationResult)
      {
        if (compilationResult->Status == SpeechRecognitionResultStatus::Success)
        {
          m_speechDetectedEventToken = m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated +=
            ref new TypedEventHandler<SpeechContinuousRecognitionSession^, SpeechContinuousRecognitionResultGeneratedEventArgs^>(
              std::bind(&VoiceInputHandler::OnResultGenerated, this, std::placeholders::_1, std::placeholders::_2)
              );
          m_speechRecognizer->ContinuousRecognitionSession->StartAsync();
          m_speechBeingDetected = true;
        }
        else
        {
          // Handle errors here.
          TrackedUltrasound::instance()->GetNotificationAPI().QueueMessage(L"Unable to compile speech patterns.");
        }
      });
    }

    //----------------------------------------------------------------------------
    VoiceInputHandler::~VoiceInputHandler()
    {
      if (m_speechBeingDetected)
      {
        m_speechRecognizer->ContinuousRecognitionSession->ResultGenerated -= m_speechDetectedEventToken;
        auto stopTask = create_task(m_speechRecognizer->ContinuousRecognitionSession->StopAsync());
        stopTask.wait();
      }
    }

    //----------------------------------------------------------------------------
    std::wstring VoiceInputHandler::GetLastCommand()
    {
      return m_lastCommandDetected;
    }

    //----------------------------------------------------------------------------
    void VoiceInputHandler::MarkCommandProcessed()
    {
      m_lastCommandDetected = L"";
    }

    //----------------------------------------------------------------------------
    void VoiceInputHandler::OnResultGenerated(SpeechContinuousRecognitionSession ^sender, SpeechContinuousRecognitionResultGeneratedEventArgs ^args)
    {
      if (args->Result->RawConfidence > MINIMUM_CONFIDENCE_FOR_DETECTION)
      {
        m_lastCommandDetected = std::wstring(args->Result->Text->Data());
      }
    }
  }
}