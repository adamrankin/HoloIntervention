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
#include "AudioFileReader.h"
#include "CardioidSound.h"
#include "OmnidirectionalSound.h"
#include "XAudio2Helpers.h"

#define HRTF_2PI 6.283185307f

namespace HoloIntervention
{
  namespace Sound
  {
    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    task<HRESULT> OmnidirectionalSound::InitializeAsync( LPCWSTR filename )
    {
      return create_task( [ = ]() -> HRESULT
      {
        m_callBack = std::make_shared<VoiceCallback>( *this );

        auto task = m_audioFile.InitializeAsync( filename );
        auto hr = task.get();

        if ( FAILED( hr ) )
        {
          throw Platform::Exception::CreateException( hr );
        }

        ComPtr<IXAPO> xapo;
        if ( SUCCEEDED( hr ) )
        {
          // Passing in nullptr as the first arg for HrtfApoInit initializes the APO with defaults of
          // omnidirectional sound with natural distance decay behavior.
          hr = CreateHrtfApo( nullptr, &xapo );
        }

        if ( SUCCEEDED( hr ) )
        {
          hr = xapo.As( &m_hrtfParams );
        }

        // Set the default environment.
        if ( SUCCEEDED( hr ) )
        {
          hr = m_hrtfParams->SetEnvironment( m_environment );
        }

        // Initialize an XAudio2 graph that hosts the HRTF xAPO.
        // The source voice is used to submit audio data and control playback.
        if ( SUCCEEDED( hr ) )
        {
          IXAudio2SourceVoice* voice = nullptr;
          hr = SetupXAudio2( m_audioFile.GetFormat(), xapo.Get(), &m_xaudio2, &voice, &( *m_callBack ) );
          if ( voice != nullptr )
          {
            // We don't need this voice as a new one is created when Start is called
            voice->DestroyVoice();
          }
        }

        m_resourcesLoaded = true;

        return hr;
      } );
    }

    //----------------------------------------------------------------------------
    OmnidirectionalSound::~OmnidirectionalSound()
    {
      std::lock_guard<std::mutex> lock( m_voiceMutex );
      for ( auto pair : m_sourceVoices )
      {
        pair.first->DestroyVoice();
      }
      m_sourceVoices.clear();
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::Start()
    {
      IXAudio2SourceVoice* voice = nullptr;
      m_xaudio2->CreateSourceVoice( &voice, m_audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, &( *m_callBack ) );
      if ( voice != nullptr )
      {
        std::lock_guard<std::mutex> lock( m_voiceMutex );
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>( m_audioFile.GetSize() );
        buffer.pAudioData = m_audioFile.GetData();
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        auto hr = voice->SubmitSourceBuffer( &buffer );

        if ( SUCCEEDED( hr ) )
        {
          m_sourceVoices[voice] = false;
          return voice->Start();
        }

        return hr;
      }

      return S_FALSE;
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::StartOnce()
    {
      IXAudio2SourceVoice* voice = nullptr;
      m_xaudio2->CreateSourceVoice( &voice, m_audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, &( *m_callBack ) );
      if ( voice != nullptr )
      {
        std::lock_guard<std::mutex> lock( m_voiceMutex );
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>( m_audioFile.GetSize() );
        buffer.pAudioData = m_audioFile.GetData();
        buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
        buffer.LoopLength = 0;
        buffer.LoopCount = 0;
        auto hr = voice->SubmitSourceBuffer( &buffer );

        if ( SUCCEEDED( hr ) )
        {
          m_sourceVoices[voice] = true;
          return voice->Start();
        }

        return hr;
      }

      return S_FALSE;
    }

    //----------------------------------------------------------------------------
    HRESULT OmnidirectionalSound::Stop()
    {
      std::lock_guard<std::mutex> lock( m_voiceMutex );
      for ( auto pair : m_sourceVoices )
      {
        pair.first->Stop();
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::SetEnvironment( HrtfEnvironment environment )
    {
      // Environment can be changed at any time.
      return m_hrtfParams->SetEnvironment( environment );
    }

    //----------------------------------------------------------------------------
    HrtfEnvironment OmnidirectionalSound::GetEnvironment()
    {
      return m_environment;
    }

    //----------------------------------------------------------------------------
    // This method is called on every dispatcher timer tick.
    // Caller passes in the information necessary to compute the source position.
    _Use_decl_annotations_
    void OmnidirectionalSound::Update( const DX::StepTimer& timer )
    {
      if( !m_resourcesLoaded )
      {
        return;
      }

      const float timeElapsed = static_cast<float>( timer.GetTotalSeconds() );

      {
        std::lock_guard<std::mutex> lock( m_voiceMutex );
        XAUDIO2_VOICE_STATE state;
        std::vector<IXAudio2SourceVoice*> toErase;
        for ( auto pair : m_sourceVoices )
        {
          if ( pair.second )
          {
            pair.first->GetState( &state );
            if ( state.BuffersQueued == 0 )
            {
              // TODO : instead of polling all voices every frame, use callback to indicate finished
              // TODO : re-use voices instead of creating/destroying
              pair.first->DestroyVoice();
              toErase.push_back( pair.first );
            }
          }
        }

        for ( auto voice : toErase )
        {
          m_sourceVoices.erase( m_sourceVoices.find( voice ) );
        }
      }

      // TODO : this class assumes a single voice... we are running multiple voices
      // TODO : create multiple sounds at the main level?
      // TODO : we need a spatialsoundAPI...
      HrtfPosition pos = { 0, 0, 0 };
      m_hrtfParams->SetSourcePosition( &pos );
    }
  }
}