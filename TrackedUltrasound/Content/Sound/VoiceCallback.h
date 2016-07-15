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