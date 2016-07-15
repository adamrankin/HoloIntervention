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

#include "pch.h"
#include "AudioFileReader.h"
#include "CardioidSound.h"
#include "OmnidirectionalSound.h"
#include "XAudio2Helpers.h"

#define HRTF_2PI 6.283185307f

namespace TrackedUltrasound
{
  namespace Sound
  {
    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    task<HRESULT> OmnidirectionalSound::InitializeAsync( LPCWSTR filename )
    {
      return create_task( [=]()->HRESULT
      {
        _callBack = std::make_shared<VoiceCallback>(*this);

        auto task = _audioFile.InitializeAsync( filename );
        auto hr = task.get();

        if (FAILED(hr))
        {
          throw Platform::Exception::CreateException(hr);
        }

        ComPtr<IXAPO> xapo;
        if ( SUCCEEDED( hr ) )
        {
          // Passing in nullptr as the first arg for HrtfApoInit initializes the APO with defaults of
          // omnidirectional sound with natural distance decay behavior.
          // CreateHrtfApo will fail with E_NOTIMPL on unsupported platforms.
          hr = CreateHrtfApo( nullptr, &xapo );
        }

        if ( SUCCEEDED( hr ) )
        {
          hr = xapo.As( &_hrtfParams );
        }

        // Set the default environment.
        if ( SUCCEEDED( hr ) )
        {
          hr = _hrtfParams->SetEnvironment( _environment );
        }

        // Initialize an XAudio2 graph that hosts the HRTF xAPO.
        // The source voice is used to submit audio data and control playback.
        if ( SUCCEEDED( hr ) )
        {
          hr = SetupXAudio2( _audioFile.GetFormat(), xapo.Get(), &_xaudio2, &_sourceVoice, &(*_callBack) );
        }

        // Submit audio data to the source voice.
        if ( SUCCEEDED( hr ) )
        {
          XAUDIO2_BUFFER buffer{};
          buffer.AudioBytes = static_cast<UINT32>( _audioFile.GetSize() );
          buffer.pAudioData = _audioFile.GetData();
          buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
          hr = _sourceVoice->SubmitSourceBuffer( &buffer );
        }

        return hr;
      } );
    }

    //----------------------------------------------------------------------------
    OmnidirectionalSound::~OmnidirectionalSound()
    {
      if ( _sourceVoice )
      {
        _sourceVoice->DestroyVoice();
      }
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::Start()
    {
      _xaudio2->CreateSourceVoice(&_sourceVoice, _audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, &(*_callBack));
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(_audioFile.GetSize());
      buffer.pAudioData = _audioFile.GetData();
      buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
      auto hr = _sourceVoice->SubmitSourceBuffer(&buffer);

      if (SUCCEEDED(hr))
      {
        _lastTick = GetTickCount64();
        return _sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::StartOnce()
    {
      _xaudio2->CreateSourceVoice(&_sourceVoice, _audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, &(*_callBack));
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(_audioFile.GetSize());
      buffer.pAudioData = _audioFile.GetData();
      buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
      buffer.LoopLength = 0;
      buffer.LoopCount = 0;
      auto hr = _sourceVoice->SubmitSourceBuffer(&buffer);

      if (SUCCEEDED(hr))
      {
        _lastTick = GetTickCount64();
        return _sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::Stop()
    {
      return _sourceVoice->Stop();
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::SetEnvironment( HrtfEnvironment environment )
    {
      // Environment can be changed at any time.
      return _hrtfParams->SetEnvironment( environment );
    }

    //----------------------------------------------------------------------------
    // This method is called on every dispatcher timer tick.
    // Caller passes in the information necessary to compute the source position.
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::OnUpdate( float angularVelocity, float height, float radius )
    {
      auto tick = GetTickCount64();
      auto elapsedTime = tick - _lastTick;
      _lastTick = tick;
      _angle += elapsedTime * angularVelocity;
      _angle = _angle > HRTF_2PI ? ( _angle - HRTF_2PI ) : _angle;
      auto position = ComputePositionInOrbit( height, radius, _angle );
      return _hrtfParams->SetSourcePosition( &position );
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HrtfPosition OmnidirectionalSound::ComputePositionInOrbit( float height, float radius, float angle )
    {
      // Calculate the position of the source based on height relative to listener's head, radius of orbit and angle relative to the listener.
      // APO uses right-handed coordinate system where negative z-axis is forward and positive z-axis is backward.
      // All coordinates use real-world units (meters).
      float x = radius * sin( angle );
      float z = -radius * cos( angle );
      return HrtfPosition{ x, height , z };
    }
  }
}