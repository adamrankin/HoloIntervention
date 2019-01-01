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
#include "Sound\AudioFileReader.h"
#include "Input\VoiceCallback.h"

// WinRT includes
#include <wrl.h>

// XAudio2 includes
#include <hrtfapoapi.h>
#include <xaudio2.h>

namespace DX
{
  class StepTimer;
}

namespace Valhalla
{
  namespace Sound
  {
    // Sound with Cardioid radiation pattern.
    class CardioidSound
    {
    public:
      CardioidSound(AudioFileReader& audioFile);
      virtual ~CardioidSound();
      HRESULT Initialize(_In_ Microsoft::WRL::ComPtr<IXAudio2> xaudio2, _In_ IXAudio2SubmixVoice* parentVoice, const Windows::Foundation::Numerics::float3& position, const Windows::Foundation::Numerics::float3& pitchYawRoll);

      HRESULT Start();
      HRESULT StartOnce();
      HRESULT Stop();

      void Update(DX::StepTimer& timer);
      HRESULT SetEnvironment(_In_ HrtfEnvironment environment);
      HrtfEnvironment GetEnvironment();

      void SetSourcePose(_In_ const Windows::Foundation::Numerics::float3& position, _In_ const Windows::Foundation::Numerics::float3& pitchYawRoll);
      Windows::Foundation::Numerics::float3& GetSourcePosition();
      Windows::Foundation::Numerics::float3& GetPitchYawRoll();

      bool IsFinished() const;

    protected:
      HrtfOrientation OrientationFromAngles(float pitch, float yaw, float roll);

    protected:
      std::shared_ptr<VoiceCallback<CardioidSound>>           m_callBack = nullptr;
      AudioFileReader&                                        m_audioFile;
      IXAudio2SourceVoice*                                    m_sourceVoice = nullptr;
      IXAudio2SubmixVoice*                                    m_submixVoice = nullptr;
      Microsoft::WRL::ComPtr<IXAPOHrtfParameters>             m_hrtfParams;

      Windows::Perception::Spatial::SpatialCoordinateSystem^  m_coordinateSystem;
      Windows::Foundation::Numerics::float3                   m_sourcePosition;
      Windows::Foundation::Numerics::float3                   m_pitchYawRoll;

      std::atomic_bool                                        m_isFinished = false;
      std::atomic_bool                                        m_resourcesLoaded = false;
      HrtfEnvironment                                         m_environment = HrtfEnvironment::Medium;
    };
  }
}