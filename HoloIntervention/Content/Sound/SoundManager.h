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

// std includes
#include <vector>

// WinRT includes
#include <ppltasks.h>

// XAudio2 includes
#include <xaudio2.h>

namespace DX
{
  class StepTimer;
}

using namespace Concurrency;
using namespace Microsoft::WRL;

namespace HoloIntervention
{
  namespace Sound
  {
    class OmnidirectionalSound;
    class CardioidSound;

    class SoundManager
    {
      template <class T> using SoundList = std::map<std::wstring, std::vector<T>>;
      typedef std::map<std::wstring, std::shared_ptr<AudioFileReader>> SoundDataList;

    public:
      virtual ~SoundManager();

      task<HRESULT> InitializeAsync();

      void PlayOmniSoundOnce( const std::wstring& assetName, HrtfEnvironment env = HrtfEnvironment::Small );
      void PlayCarioidSoundOnce( const std::wstring& assetName, HrtfEnvironment env = HrtfEnvironment::Small );

      void Update( DX::StepTimer& stepTimer, float angularVelocity, float height, float radius );

    protected:
      HRESULT CreateSubmixParentVoices();

    protected:
      // XAudio2 assets
      ComPtr<IXAudio2>                                              m_xaudio2 = nullptr;
      ComPtr<IXAudio2MasteringVoice>                                m_masterVoice = nullptr;

      ComPtr<IXAudio2SubmixVoice>                                   m_omniSubmixParentVoice = nullptr;
      ComPtr<IXAudio2SubmixVoice>                                   m_cardioidSubmixParentVoice = nullptr;

      SoundList<std::shared_ptr<Sound::CardioidSound>>              m_cardioidSounds;
      SoundList<std::shared_ptr<Sound::OmnidirectionalSound>>       m_omniDirectionalSounds;
      SoundDataList                                                 m_audioAssets;

      bool                                                          m_resourcesLoaded = false;
    };
  }
}