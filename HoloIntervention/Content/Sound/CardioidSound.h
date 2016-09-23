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
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Sound
  {
    // Sound with Cardioid radiation pattern.
    class CardioidSound
    {
    public:
      CardioidSound( AudioFileReader& audioFile );
      virtual ~CardioidSound();
      HRESULT Initialize( _In_ ComPtr<IXAudio2> xaudio2, _In_ IXAudio2SubmixVoice* parentVoice, SpatialCoordinateSystem^ coordinateSystem, const float3& position, const float3& pitchYawRoll );

      HRESULT Start();
      HRESULT StartOnce();
      HRESULT Stop();

      void Update( DX::StepTimer& timer );
      HRESULT SetEnvironment( _In_ HrtfEnvironment environment );
      HrtfEnvironment GetEnvironment();

      void SetSourcePose( _In_ SpatialCoordinateSystem^ coordinateSystem, _In_ const float3& position, _In_ const float3& pitchYawRoll );
      float3& GetSourcePosition();
      float3& GetPitchYawRoll();

    protected:
      HrtfOrientation OrientationFromAngles( float pitch, float yaw, float roll );

    protected:
      std::shared_ptr<VoiceCallback<CardioidSound>>   m_callBack = nullptr;
      AudioFileReader&                                m_audioFile;
      IXAudio2SourceVoice*                            m_sourceVoice = nullptr;
      IXAudio2SubmixVoice*                            m_submixVoice = nullptr;
      ComPtr<IXAPOHrtfParameters>                     m_hrtfParams;

      SpatialCoordinateSystem^                        m_coordinateSystem;
      float3                                          m_sourcePosition;
      float3                                          m_pitchYawRoll;

      bool                                            m_isFinished = false;
      bool                                            m_resourcesLoaded = false;
      HrtfEnvironment                                 m_environment = HrtfEnvironment::Medium;
    };
  }
}