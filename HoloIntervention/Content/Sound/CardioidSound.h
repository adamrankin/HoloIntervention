//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

// Local includes
#include "AudioFileReader.h"
#include "VoiceCallback.h"

// WinRT includes
#include <wrl.h>
#include <ppltasks.h>

// XAudio2 includes
#include <hrtfapoapi.h>
#include <xaudio2.h>

using namespace Concurrency;
using namespace Microsoft::WRL;

namespace HoloIntervention
{
  namespace Sound
  {
    // Sound with Cardioid radiation pattern.
    class CardioidSound
    {
    public:
      virtual ~CardioidSound();
      task<HRESULT> InitializeAsync( _In_ ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, _In_ const std::wstring& filename );
      HRESULT ConfigureApo( _In_ ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, _In_ float scaling, _In_ float order );

      HRESULT Start();
      HRESULT Stop();
      HRESULT OnUpdate( _In_ float x, _In_ float, _In_ float z, _In_ float pitch, _In_ float yaw, _In_ float roll );
      HRESULT SetEnvironment( _In_ HrtfEnvironment environment );
      HrtfEnvironment GetEnvironment();

    protected:
      HrtfOrientation OrientationFromAngles( float pitch, float yaw, float roll );

    protected:
      std::shared_ptr<VoiceCallback<CardioidSound>>   m_callBack = nullptr;
      AudioFileReader                                 m_audioFile;
      IXAudio2SourceVoice*                            m_sourceVoice = nullptr;
      IXAudio2SubmixVoice*                            m_submixVoice = nullptr;
      ComPtr<IXAPOHrtfParameters>                     m_hrtfParams;
      HrtfEnvironment                                 m_environment = HrtfEnvironment::Medium;
    };
  }
}