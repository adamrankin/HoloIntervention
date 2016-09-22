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
    _Use_decl_annotations_
    task<HRESULT> CardioidSound::InitializeAsync( ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, const std::wstring& filename )
    {
      return create_task( [ = ]()->HRESULT
      {
        auto initTask = m_audioFile.InitializeAsync( filename );
        auto hr = initTask.get();

        if ( FAILED( hr ) )
        {
          throw Platform::Exception::CreateException( hr );
        }

        if ( SUCCEEDED( hr ) )
        {
          // Initialize with "Scaling" fully directional and "Order" with broad radiation pattern.
          // As the order goes higher, the cardioid directivity region becomes narrower.
          // Any direct path signal outside of the directivity region will be attenuated based on the scaling factor.
          // For example, if scaling is set to 1 (fully directional) the direct path signal outside of the directivity
          // region will be fully attenuated and only the reflections from the environment will be audible.
          hr = ConfigureApo( xaudio2, parentVoice, 1.0f, 4.0f );
        }
        return hr;
      } );
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
    HRESULT CardioidSound::ConfigureApo( ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, float scaling, float order )
    {
      if ( m_hrtfParams )
      {
        m_hrtfParams = nullptr;
      }

      m_callBack = std::make_shared<VoiceCallback<CardioidSound>>( *this );

      // Cardioid directivity configuration
      HrtfDirectivityCardioid cardioid;
      cardioid.directivity.type = HrtfDirectivityType::Cardioid;
      cardioid.directivity.scaling = scaling;
      cardioid.order = order;

      // APO initialization
      HrtfApoInit apoInit;
      apoInit.directivity = &cardioid.directivity;
      apoInit.distanceDecay = nullptr;                // This specifies natural distance decay behavior (simulates real world)

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

      if ( FAILED( hr ) )
      {
        throw Platform::Exception::CreateException( hr );
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CardioidSound::Start()
    {
      return m_sourceVoice->Start();
    }

    //----------------------------------------------------------------------------
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
    HrtfEnvironment CardioidSound::GetEnvironment()
    {
      return m_environment;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT CardioidSound::OnUpdate( float x, float y, float z, float pitch, float yaw, float roll )
    {
      auto hr = S_OK;
      if ( m_hrtfParams )
      {
        auto position = HrtfPosition{ x, y, z };
        hr = m_hrtfParams->SetSourcePosition( &position );
        if ( SUCCEEDED( hr ) )
        {
          auto sourceOrientation = OrientationFromAngles( pitch, yaw, roll );
          hr = m_hrtfParams->SetSourceOrientation( &sourceOrientation );
        }
      }
      return hr;
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