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
#include "CardioidSound.h"

// Common includes
#include "StepTimer.h"

// XAudio2 includes
#include <xapo.h>

// WinRT includes
#include <wrl.h>

using namespace Microsoft::WRL;

namespace HoloIntervention
{
  namespace Sound
  {
    //----------------------------------------------------------------------------
    CardioidSound::CardioidSound( AudioFileReader& audioFile )
      : m_audioFile( audioFile )
    {

    }

    //----------------------------------------------------------------------------
    CardioidSound::~CardioidSound()
    {
      m_sourceVoice->DestroyVoice();
      m_submixVoice->DestroyVoice();
      m_callBack = nullptr;
      m_hrtfParams = nullptr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::Initialize( ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, const float3& position, const float3& pitchYawRoll )
    {
      if ( m_hrtfParams )
      {
        m_hrtfParams = nullptr;
      }

      m_callBack = std::make_shared<VoiceCallback<CardioidSound>>( *this );

      // Cardioid directivity configuration
      HrtfDirectivityCardioid cardioid;
      cardioid.directivity.type = HrtfDirectivityType::Cardioid;
      cardioid.directivity.scaling = 1.f;
      cardioid.order = 4.f;

      // APO initialization
      HrtfApoInit apoInit;
      apoInit.directivity = &cardioid.directivity;
      apoInit.distanceDecay = nullptr;  // This specifies natural distance decay behavior (simulates real world)

      // CreateHrtfApo will fail with E_NOTIMPL on unsupported platforms.
      ComPtr<IXAPO> xapo;
      auto hr = CreateHrtfApo( &apoInit, &xapo );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }

      hr = xapo.As( &m_hrtfParams );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }
      // Set the initial environment.
      // Environment settings configure the "distance cues" used to compute the early and late reverberations.
      hr = m_hrtfParams->SetEnvironment( m_environment );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }

      // Create a source voice to accept audio data in the specified format.
      hr = xaudio2->CreateSourceVoice( &m_sourceVoice, m_audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, m_callBack.get() );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }

      // Create a submix voice that will host the xAPO.
      XAUDIO2_EFFECT_DESCRIPTOR fxDesc{};
      fxDesc.InitialState = TRUE;
      fxDesc.OutputChannels = 2;          // Stereo output
      fxDesc.pEffect = xapo.Get();              // HRTF xAPO set as the effect.

      XAUDIO2_EFFECT_CHAIN fxChain{};
      fxChain.EffectCount = 1;
      fxChain.pEffectDescriptors = &fxDesc;

      XAUDIO2_VOICE_SENDS sends = {};
      XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
      sendDesc.pOutputVoice = parentVoice;
      sends.SendCount = 1;
      sends.pSends = &sendDesc;

      // HRTF APO expects mono 48kHz input, so we configure the submix voice for that format.
      hr = xaudio2->CreateSubmixVoice( &m_submixVoice, 1, 48000, 0, 0, &sends, nullptr );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }

      // Route the source voice to the submix voice.
      // The complete graph pipeline looks like this -
      // Source Voice -> Submix Voice (HRTF xAPO) -> Mastering Voice
      sends = {};
      sendDesc = {};
      sendDesc.pOutputVoice = m_submixVoice;
      sends.SendCount = 1;
      sends.pSends = &sendDesc;
      hr = m_sourceVoice->SetOutputVoices( &sends );

      SetSourcePose( position, pitchYawRoll );

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }
      else
      {
        m_resourcesLoaded = true;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::Start()
    {
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>( m_audioFile.GetSize() );
      buffer.pAudioData = m_audioFile.GetData();
      buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
      auto hr = m_sourceVoice->SubmitSourceBuffer( &buffer );

      if ( SUCCEEDED( hr ) )
      {
        m_isFinished = false;
        return m_sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::StartOnce()
    {
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>( m_audioFile.GetSize() );
      buffer.pAudioData = m_audioFile.GetData();
      buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
      buffer.LoopLength = 0;
      buffer.LoopCount = 0;
      auto hr = m_sourceVoice->SubmitSourceBuffer( &buffer );

      if ( SUCCEEDED( hr ) )
      {
        m_isFinished = false;
        return m_sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::Stop()
    {
      return m_sourceVoice->Stop();
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::SetEnvironment( HrtfEnvironment environment )
    {
      // Environment can be changed at any time.
      return m_hrtfParams->SetEnvironment( environment );
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HrtfEnvironment CardioidSound::GetEnvironment()
    {
      return m_environment;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    void CardioidSound::SetSourcePose( const float3& position, const float3& pitchYawRoll )
    {
      m_sourcePosition = position;
      auto hrtf_position = HrtfPosition{ position.x, position.y, position.z };
      m_hrtfParams->SetSourcePosition( &hrtf_position );

      auto sourceOrientation = OrientationFromAngles( pitchYawRoll.x, pitchYawRoll.y, pitchYawRoll.z );
      m_hrtfParams->SetSourceOrientation( &sourceOrientation );
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    float3& CardioidSound::GetSourcePosition()
    {
      return m_sourcePosition;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    float3& CardioidSound::GetPitchYawRoll()
    {
      return m_pitchYawRoll;
    }

    //----------------------------------------------------------------------------
    bool CardioidSound::IsFinished() const
    {
      return m_isFinished;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    void CardioidSound::Update( DX::StepTimer& timer )
    {
      if ( !m_resourcesLoaded || m_isFinished )
      {
        return;
      }

      const float timeElapsed = static_cast<float>( timer.GetTotalSeconds() );

      XAUDIO2_VOICE_STATE state;
      m_sourceVoice->GetState( &state );
      if ( state.BuffersQueued == 0 )
      {
        m_isFinished = true;
      }
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HrtfOrientation CardioidSound::OrientationFromAngles( float pitch, float yaw, float roll )
    {
      // Negate all angles for right handed coordinate system.
      DirectX::XMFLOAT3 angles{ -pitch, -yaw, -roll };
      DirectX::XMVECTOR vector = DirectX::XMLoadFloat3( &angles );
      DirectX::XMMATRIX rm = DirectX::XMMatrixRotationRollPitchYawFromVector( vector );
      DirectX::XMFLOAT3X3 rm33{};
      DirectX::XMStoreFloat3x3( &rm33, rm );
      return HrtfOrientation{ rm33._11, rm33._12, rm33._13, rm33._21, rm33._22, rm33._23, rm33._31, rm33._32, rm33._33 };
    }
  }
}