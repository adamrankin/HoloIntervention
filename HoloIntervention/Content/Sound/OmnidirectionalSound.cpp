/*====================================================================
Copyright(c) 2017 Adam Rankin


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

// Local includes
#include "pch.h"
#include "AudioFileReader.h"
#include "OmnidirectionalSound.h"

// Common includes
#include "StepTimer.h"

// XAudio2 includes
#include <xapo.h>
#include <xaudio2.h>

#define HRTF_2PI 6.283185307f

using namespace Windows::Foundation::Numerics;
using namespace Microsoft::WRL;

namespace HoloIntervention
{
  namespace Sound
  {
    //----------------------------------------------------------------------------
    OmnidirectionalSound::OmnidirectionalSound(AudioFileReader& audioFile)
      : m_audioFile(audioFile)
    {

    }

    //----------------------------------------------------------------------------
    OmnidirectionalSound::~OmnidirectionalSound()
    {
      m_sourceVoice->DestroyVoice();
      m_sourceVoice = nullptr;
      m_hrtfParams = nullptr;
      m_resourcesLoaded = false;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::Initialize(ComPtr<IXAudio2> xaudio2, IXAudio2SubmixVoice* parentVoice, const float3& position)
    {
      m_callBack = std::make_shared<VoiceCallback<OmnidirectionalSound>>(*this);

      ComPtr<IXAPO> xapo;
      // Passing in nullptr as the first arg for HrtfApoInit initializes the APO with defaults of
      // omnidirectional sound with natural distance decay behavior.
      auto hr = CreateHrtfApo(nullptr, &xapo);

      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }

      hr = xapo.As(&m_hrtfParams);

      // Set the default environment.
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }

      hr = m_hrtfParams->SetEnvironment(m_environment);

      // Initialize an XAudio2 graph that hosts the HRTF xAPO.
      // The source voice is used to submit audio data and control playback.
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }

      hr = xaudio2->CreateSourceVoice(&m_sourceVoice, m_audioFile.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO, m_callBack.get());
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }

      XAUDIO2_VOICE_SENDS sends = {};
      XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
      sendDesc.pOutputVoice = parentVoice;
      sends.SendCount = 1;
      sends.pSends = &sendDesc;
      hr = m_sourceVoice->SetOutputVoices(&sends);
      if (FAILED(hr))
      {
        throw Platform::Exception::CreateException(hr);
      }

      m_sourcePosition = position;

      m_resourcesLoaded = true;

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::Start()
    {
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(m_audioFile.GetSize());
      buffer.pAudioData = m_audioFile.GetData();
      buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
      auto hr = m_sourceVoice->SubmitSourceBuffer(&buffer);

      if (SUCCEEDED(hr))
      {
        m_isFinished = false;
        return m_sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::StartOnce()
    {
      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(m_audioFile.GetSize());
      buffer.pAudioData = m_audioFile.GetData();
      buffer.LoopBegin = XAUDIO2_NO_LOOP_REGION;
      buffer.LoopLength = 0;
      buffer.LoopCount = 0;
      auto hr = m_sourceVoice->SubmitSourceBuffer(&buffer);

      if (SUCCEEDED(hr))
      {
        m_isFinished = false;
        return m_sourceVoice->Start();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::Stop()
    {
      if (m_sourceVoice != nullptr)
      {
        m_sourceVoice->Stop();
        m_isFinished = true;
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HRESULT OmnidirectionalSound::SetEnvironment(HrtfEnvironment environment)
    {
      if (m_hrtfParams == nullptr)
      {
        return E_FAIL;
      }

      // Environment can be changed at any time.
      return m_hrtfParams->SetEnvironment(environment);
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    HrtfEnvironment OmnidirectionalSound::GetEnvironment()
    {
      return m_environment;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    bool OmnidirectionalSound::IsFinished() const
    {
      return m_isFinished;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    void OmnidirectionalSound::SetSourcePosition(const float3& position)
    {
      m_sourcePosition = position;

      HrtfPosition hrtf_position = { position.x, position.y, position.z };
      m_hrtfParams->SetSourcePosition(&hrtf_position);
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    float3& OmnidirectionalSound::GetSourcePosition()
    {
      return m_sourcePosition;
    }

    //----------------------------------------------------------------------------
    _Use_decl_annotations_
    void OmnidirectionalSound::Update(const DX::StepTimer& timer)
    {
      if (!m_resourcesLoaded  || m_isFinished)
      {
        return;
      }

      const float timeElapsed = static_cast<float>(timer.GetTotalSeconds());

      XAUDIO2_VOICE_STATE state;
      m_sourceVoice->GetState(&state);
      if (state.BuffersQueued == 0)
      {
        m_isFinished = true;
      }
    }
  }
}