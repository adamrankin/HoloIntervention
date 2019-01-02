/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Sound\SoundAPI.h"
#include "OmnidirectionalSound.h"
#include "CardioidSound.h"

// Common includes
#include "Common\StepTimer.h"

// XAudio2 includes
#include <xapo.h>
#include <xaudio2.h>

// STL includes
#include <sstream>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace Valhalla
{
  namespace Sound
  {
    static const uint32_t SOUND_ASSET_COUNT = 3;
    static std::wstring SOUND_ASSET_FILENAMES[SOUND_ASSET_COUNT][2] =
    {
      { L"cursor_toggle", L"Assets/Sounds/cursor_toggle.wav" },
      { L"input_fail", L"Assets/Sounds/input_fail.mp3" },
      { L"input_ok", L"Assets/Sounds/input_ok.mp3" }
    };

    //----------------------------------------------------------------------------
    SoundAPI::~SoundAPI()
    {
      m_componentReady = false;
      m_cardioidSounds.clear();
      m_omniDirectionalSounds.clear();
      m_audioAssets.clear();

      if(m_cardioidSubmixParentVoice != nullptr)
      {
        m_cardioidSubmixParentVoice->DestroyVoice();
      }

      if(m_omniSubmixParentVoice != nullptr)
      {
        m_omniSubmixParentVoice->DestroyVoice();
      }

      if(m_masterVoice != nullptr)
      {
        m_masterVoice->DestroyVoice();
      }

      m_xaudio2 = nullptr;
    }

    //----------------------------------------------------------------------------
    task<HRESULT> SoundAPI::InitializeAsync()
    {
      return create_task([this]() -> HRESULT
      {
        auto hr = XAudio2Create(&m_xaudio2, XAUDIO2_1024_QUANTUM);

        if(FAILED(hr))
        {
          LOG(LOG_LEVEL_ERROR, "Cannot initialize audio system.");
          return hr;
        }

        // HRTF APO expects mono audio data at 48kHz and produces stereo output at 48kHz
        // so we create a stereo mastering voice with specific rendering sample rate of 48kHz.
        // Mastering voice will be automatically destroyed when XAudio2 instance is destroyed.
        hr = m_xaudio2->CreateMasteringVoice(&m_masterVoice, 2, 48000);

        if(FAILED(hr))
        {
          throw Platform::Exception::CreateException(hr);
        }

        hr = CreateSubmixParentVoices();

        if(FAILED(hr))
        {
          throw Platform::Exception::CreateException(hr);
        }

        // Initialize ALL sound assets into memory
        for(auto pair : SOUND_ASSET_FILENAMES)
        {
          auto& name = pair[0];
          auto& fileName = pair[1];

          std::shared_ptr<AudioFileReader> fileReader = std::make_shared<AudioFileReader>();
          auto initTask = fileReader->InitializeAsync(fileName);
          HRESULT hr = initTask.get();

          if(FAILED(hr))
          {
            throw Platform::Exception::CreateException(hr);
          }

          m_audioAssets[name] = fileReader;
        }

        m_componentReady = true;

        return S_OK;
      });
    }

    //----------------------------------------------------------------------------
    void SoundAPI::PlayOmniSoundOnce(const std::wstring& assetName, SpatialCoordinateSystem^ coordinateSystem, const float3& position, HrtfEnvironment env /* = HrtfEnvironment::Small */)
    {
      if(m_audioAssets.find(assetName) == m_audioAssets.end()
          || m_coordinateSystem == nullptr)
      {
        return;
      }

      auto omniSound = std::make_shared<OmnidirectionalSound>(*m_audioAssets[assetName].get());

      float3 positionCopy = position;

      if(coordinateSystem != nullptr)
      {
        auto transform = coordinateSystem->TryGetTransformTo(m_coordinateSystem);

        if(transform != nullptr)
        {
          XMMATRIX mat = XMLoadFloat4x4(&transform->Value);
          XMVECTOR pos = XMLoadFloat3(&position);
          XMVECTOR transformedPos = XMVector3Transform(pos, mat);
          positionCopy.x = transformedPos.m128_f32[0];
          positionCopy.y = transformedPos.m128_f32[1];
          positionCopy.z = transformedPos.m128_f32[2];
        }
      }

      HRESULT hr;

      try
      {
        hr = omniSound->Initialize(m_xaudio2, m_omniSubmixParentVoice, positionCopy);
      }
      catch(Platform::Exception^ e)
      {
        WLOG(LOG_LEVEL_ERROR, e->Message);
        omniSound = nullptr;
      }

      if(FAILED(hr))
      {
        WLOG(LOG_LEVEL_ERROR, std::wstring(L"Unable to initialize sound. ") + assetName);
        omniSound = nullptr;
        return;
      }

      hr = omniSound->SetEnvironment(env);

      if(FAILED(hr))
      {
        WLOG(LOG_LEVEL_ERROR, std::wstring(L"Unable to set sound environment. ") + assetName);
        omniSound = nullptr;
        return;
      }

      m_omniDirectionalSounds[assetName].push_back(omniSound);

      omniSound->StartOnce();
    }

    //----------------------------------------------------------------------------
    void SoundAPI::PlayCarioidSoundOnce(const std::wstring& assetName, SpatialCoordinateSystem^ coordinateSystem, const float3& position, const float3& pitchYawRoll, HrtfEnvironment env /*= HrtfEnvironment::Small */)
    {
      if(m_audioAssets.find(assetName) == m_audioAssets.end()
          || m_coordinateSystem == nullptr)
      {
        return;
      }

      auto cardioidSound = std::make_shared<Valhalla::Sound::CardioidSound>(*m_audioAssets[assetName].get());

      float3 positionCopy = position;

      if(coordinateSystem != nullptr)
      {
        auto transform = coordinateSystem->TryGetTransformTo(m_coordinateSystem);

        if(transform != nullptr)
        {
          XMMATRIX mat = XMLoadFloat4x4(&transform->Value);
          XMVECTOR pos = XMLoadFloat3(&position);
          XMVECTOR transformedPos = XMVector3Transform(pos, mat);
          positionCopy.x = transformedPos.m128_f32[0];
          positionCopy.y = transformedPos.m128_f32[1];
          positionCopy.z = transformedPos.m128_f32[2];
        }
      }

      HRESULT hr;

      try
      {
        hr = cardioidSound->Initialize(m_xaudio2, m_cardioidSubmixParentVoice, positionCopy, pitchYawRoll);
      }
      catch(Platform::Exception^ e)
      {
        WLOG(LOG_LEVEL_ERROR, e->Message);
        cardioidSound = nullptr;
      }

      if(FAILED(hr))
      {
        WLOG(LOG_LEVEL_ERROR, std::wstring(L"Unable to initialize sound. ") + assetName);
        cardioidSound = nullptr;
        return;
      }

      hr = cardioidSound->SetEnvironment(env);

      if(FAILED(hr))
      {
        WLOG(LOG_LEVEL_ERROR, std::wstring(L"Unable to set sound environment. ") + assetName);
        cardioidSound = nullptr;
        return;
      }

      m_cardioidSounds[assetName].push_back(cardioidSound);

      cardioidSound->StartOnce();
    }

    //----------------------------------------------------------------------------
    void SoundAPI::Update(DX::StepTimer& stepTimer, SpatialCoordinateSystem^ coordinateSystem)
    {
      m_coordinateSystem = coordinateSystem;

      for(auto& pair : m_cardioidSounds)
      {
        for(auto iter = pair.second.begin(); iter != pair.second.end();)
        {
          auto& sound = *iter;
          sound->Update(stepTimer);

          if(sound->IsFinished())
          {
            iter = pair.second.erase(iter);
          }
          else
          {
            iter++;
          }
        }
      }

      for(auto& pair : m_omniDirectionalSounds)
      {
        for(auto iter = pair.second.begin(); iter != pair.second.end();)
        {
          auto& sound = *iter;
          sound->Update(stepTimer);

          if(sound->IsFinished())
          {
            iter = pair.second.erase(iter);
          }
          else
          {
            iter++;
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    HRESULT SoundAPI::CreateSubmixParentVoices()
    {
      // Omni
      XAUDIO2_VOICE_SENDS sends = {};
      XAUDIO2_SEND_DESCRIPTOR sendDesc = {};
      sendDesc.pOutputVoice = m_masterVoice;
      sends.SendCount = 1;
      sends.pSends = &sendDesc;
      auto hr = m_xaudio2->CreateSubmixVoice(&m_omniSubmixParentVoice, 1, 48000, 0, 0, &sends, nullptr);

      if(FAILED(hr))
      {
        return hr;
      }

      // Cardioid
      sends = {};
      sendDesc = {};
      sendDesc.pOutputVoice = m_masterVoice;
      sends.SendCount = 1;
      sends.pSends = &sendDesc;
      return hr = m_xaudio2->CreateSubmixVoice(&m_omniSubmixParentVoice, 1, 48000, 0, 0, &sends, nullptr);
    }
  }
}