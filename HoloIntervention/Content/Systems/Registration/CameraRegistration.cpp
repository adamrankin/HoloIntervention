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

// Local includes
#include "pch.h"
#include "AppView.h"
#include "CameraRegistration.h"

// Common includes
#include "DeviceResources.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace Windows::Perception::Spatial;
using namespace Windows::Media::MediaProperties;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    CameraRegistration::CameraRegistration(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      m_initTask = m_captureDevice->InitializeAsync(nullptr).then([this]()
      {
        m_initialized = true;
      });
    }

    //----------------------------------------------------------------------------
    CameraRegistration::~CameraRegistration()
    {
      if (m_initialized)
      {
        m_captureDevice->CleanupAsync();
        m_initialized = false;
      }
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      if (!m_initialized)
      {
        return;
      }

      m_captureDevice->SetCoordinateSystem(coordinateSystem);
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"start camera"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!m_initialized)
        {
          create_task([this]()
          {
            uint32_t accumulator(0);
            while (!m_captureDevice->Initialized && accumulator < 10000) // 10s timeout
            {
              std::this_thread::sleep_for(std::chrono::milliseconds(100));
              accumulator += 100;
            }
            return m_captureDevice->Initialized;
          }).then([this](bool initialized)
          {
            if (m_recording || !initialized)
            {
              return;
            }

            try
            {
              m_captureDevice->StartRecordingAsync(MediaEncodingProfile::CreateMp4(VideoEncodingQuality::Auto)).then([this]()
              {
                m_recording = true;
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing...");
              });
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(e->Message->Data());
            }
          });
        }
        else
        {
          m_captureDevice->StartRecordingAsync(MediaEncodingProfile::CreateMp4(VideoEncodingQuality::Auto)).then([this]()
          {
            m_recording = true;
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing...");
          });
        }
      };

      callbackMap[L"stop camera"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_recording)
        {
          m_captureDevice->StopRecordingAsync().then([this]()
          {
            // TODO : grab frames, and process
            m_recording = false;
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing stopped.");
          });
        }
      };
    }
  }
}