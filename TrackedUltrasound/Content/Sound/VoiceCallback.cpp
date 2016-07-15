
#include "pch.h"
#include "VoiceCallback.h"
#include "OmnidirectionalSound.h"

namespace TrackedUltrasound
{
  namespace Sound
  {
    //----------------------------------------------------------------------------
    VoiceCallback::VoiceCallback( OmnidirectionalSound& sound )
      : m_sound( &sound)
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnStreamEnd()
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnVoiceProcessingPassEnd()
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnVoiceProcessingPassStart(UINT32 SamplesRequired)
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnBufferEnd(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnBufferStart(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnLoopEnd(void* pBufferContext)
    {

    }

    //----------------------------------------------------------------------------
    void VoiceCallback::OnVoiceError(void* pBufferContext, HRESULT Error)
    {

    }

    //----------------------------------------------------------------------------
    TrackedUltrasound::Sound::VoiceCallback::~VoiceCallback()
    {

    }
  }
}