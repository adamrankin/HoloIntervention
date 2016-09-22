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

namespace HoloIntervention
{
  namespace Sound
  {
    template<class T>
    class VoiceCallback : public IXAudio2VoiceCallback
    {
    public:
      VoiceCallback( T& sound );
      ~VoiceCallback();

      virtual void STDMETHODCALLTYPE OnStreamEnd();
      virtual void STDMETHODCALLTYPE OnVoiceProcessingPassEnd();
      virtual void STDMETHODCALLTYPE OnVoiceProcessingPassStart( UINT32 SamplesRequired );
      virtual void STDMETHODCALLTYPE OnBufferEnd( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnBufferStart( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnLoopEnd( void* pBufferContext );
      virtual void STDMETHODCALLTYPE OnVoiceError( void* pBufferContext, HRESULT Error );

    private:
      T& m_sound;
    };

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnVoiceError(void* pBufferContext, HRESULT Error)
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnLoopEnd(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnBufferStart(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnBufferEnd(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnVoiceProcessingPassStart(UINT32 SamplesRequired)
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnVoiceProcessingPassEnd()
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    void STDMETHODCALLTYPE HoloIntervention::Sound::VoiceCallback<T>::OnStreamEnd()
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    HoloIntervention::Sound::VoiceCallback<T>::~VoiceCallback()
    {

    }

    //----------------------------------------------------------------------------
    template<class T>
    HoloIntervention::Sound::VoiceCallback<T>::VoiceCallback( T& sound )
      : m_sound( sound )
    {

    }

  }
}