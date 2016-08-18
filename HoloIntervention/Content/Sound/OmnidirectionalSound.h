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
#include "AudioFileReader.h"
#include "StepTimer.h"
#include "VoiceCallback.h"
#include "XAudio2Helpers.h"

// WinRT includes
#include <hrtfapoapi.h>
#include <ppltasks.h>
#include <wrl.h>

// STL includes
#include <map>

using namespace Microsoft::WRL;
using namespace Concurrency;

namespace HoloIntervention
{
  namespace Sound
  {
    // Sound with omnidirectional radiation pattern i.e. emits sound equally in all directions.
    class OmnidirectionalSound
    {
    public:
      virtual ~OmnidirectionalSound();
      task<HRESULT> InitializeAsync( _In_ LPCWSTR filename );

      HRESULT Start();
      HRESULT StartOnce();
      HRESULT Stop();
      void Update( const DX::StepTimer& timer );
      HRESULT SetEnvironment( _In_ HrtfEnvironment environment );
      HrtfEnvironment GetEnvironment();

    protected:
      std::shared_ptr<VoiceCallback>          m_callBack = nullptr;
      AudioFileReader                         m_audioFile;
      ComPtr<IXAudio2>                        m_xaudio2;
      std::map<IXAudio2SourceVoice*, bool>    m_sourceVoices;
      ComPtr<IXAPOHrtfParameters>             m_hrtfParams;
      HrtfEnvironment                         m_environment = HrtfEnvironment::Outdoors;
      std::mutex                              m_voiceMutex;
      bool                                    m_resourcesLoaded = false;
    };
  }
}