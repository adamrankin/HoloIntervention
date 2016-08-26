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

// STD includes
#include <functional>
#include <map>
#include <string>

using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace Input
  {
    // command, function
    typedef std::map<std::wstring, std::function<void()>> VoiceInputCallbackMap;

    class VoiceInputHandler
    {
    public:
      VoiceInputHandler();
      ~VoiceInputHandler();

      void RegisterCallbacks( VoiceInputCallbackMap& callbacks );

    protected:
      void OnResultGenerated( SpeechContinuousRecognitionSession^ sender, SpeechContinuousRecognitionResultGeneratedEventArgs^ args );

    protected:
      // Used for cleaning up
      bool                                                  m_speechBeingDetected = false;

      // API objects used to process voice input
      SpeechRecognizer^                                     m_speechRecognizer;

      // Store the command related details
      VoiceInputCallbackMap                                 m_callbacks;

      // Event registration token.
      Windows::Foundation::EventRegistrationToken           m_speechDetectedEventToken;

      const float                                           MINIMUM_CONFIDENCE_FOR_DETECTION = 0.4f; // [0,1]
    };
  }
}