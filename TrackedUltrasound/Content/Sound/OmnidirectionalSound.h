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
#include "XAudio2Helpers.h"

// WinRT includes
#include <hrtfapoapi.h>
#include <ppltasks.h>
#include <wrl.h>

// STL includes
#include <map>

using namespace Microsoft::WRL;
using namespace concurrency;

namespace TrackedUltrasound
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
      HRESULT OnUpdate( _In_ float angularVelocity, _In_ float height, _In_ float radius );
      HRESULT SetEnvironment( _In_ HrtfEnvironment environment );
      HrtfEnvironment GetEnvironment()
      {
        return _environment;
      }

    private:
      HrtfPosition ComputePositionInOrbit( _In_ float height, _In_ float radius, _In_ float angle );

    private:
      std::shared_ptr<VoiceCallback>          _callBack = nullptr;
      AudioFileReader                         _audioFile;
      ComPtr<IXAudio2>                        _xaudio2;
      std::map<IXAudio2SourceVoice*, bool>    _sourceVoices;
      ComPtr<IXAPOHrtfParameters>             _hrtfParams;
      HrtfEnvironment                         _environment = HrtfEnvironment::Outdoors;
      ULONGLONG                               _lastTick = 0;
      float                                   _angle = 0;
    };
  }
}