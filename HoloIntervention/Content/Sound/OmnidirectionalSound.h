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

// WinRT includes
#include <hrtfapoapi.h>
#include <ppltasks.h>
#include <wrl.h>

// STL includes
#include <map>

using namespace Concurrency;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace Sound
  {
    // Sound with omnidirectional radiation pattern i.e. emits sound equally in all directions.
    class OmnidirectionalSound
    {
    public:
      OmnidirectionalSound( AudioFileReader& audioFile );
      virtual ~OmnidirectionalSound();
      HRESULT Initialize( _In_ ComPtr<IXAudio2> xaudio2, _In_ IXAudio2SubmixVoice* parentVoice, _In_ const float3& position );

      HRESULT Start();
      HRESULT StartOnce();
      HRESULT Stop();

      void Update( const DX::StepTimer& timer );
      HRESULT SetEnvironment( _In_ HrtfEnvironment environment );
      HrtfEnvironment GetEnvironment();

      bool IsFinished() const;

      void SetSourcePosition( _In_ const float3& position );
      float3& GetSourcePosition();

    protected:
      std::shared_ptr<VoiceCallback<OmnidirectionalSound>>    m_callBack = nullptr;
      AudioFileReader&                                        m_audioFile;
      IXAudio2SourceVoice*                                    m_sourceVoice;
      ComPtr<IXAPOHrtfParameters>                             m_hrtfParams;
      HrtfEnvironment                                         m_environment = HrtfEnvironment::Medium;
      float3                                                  m_sourcePosition;

      bool                                                    m_isFinished = false;
      bool                                                    m_resourcesLoaded = false;
    };
  }
}