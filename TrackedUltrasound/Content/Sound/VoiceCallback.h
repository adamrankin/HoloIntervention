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

#include <concrt.h>
#include <xaudio2.h>
#include <basetyps.h>

namespace TrackedUltrasound
{
  namespace Sound
  {
    class OmnidirectionalSound;

    class VoiceCallback : public IXAudio2VoiceCallback
    {
    public:
      VoiceCallback( OmnidirectionalSound& sound );
      ~VoiceCallback();

      virtual void STDMETHODCALLTYPE OnStreamEnd();
      virtual void STDMETHODCALLTYPE OnVoiceProcessingPassEnd();
      virtual void STDMETHODCALLTYPE OnVoiceProcessingPassStart( UINT32 SamplesRequired );
      virtual void STDMETHODCALLTYPE OnBufferEnd( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnBufferStart( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnLoopEnd( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnVoiceError( void* pBufferContext, HRESULT Error );

    private:
      OmnidirectionalSound* m_sound;
    };
  }
}