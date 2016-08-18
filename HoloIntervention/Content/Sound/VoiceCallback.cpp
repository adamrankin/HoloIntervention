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

#include "pch.h"
#include "VoiceCallback.h"
#include "OmnidirectionalSound.h"

namespace HoloIntervention
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
    HoloIntervention::Sound::VoiceCallback::~VoiceCallback()
    {

    }
  }
}