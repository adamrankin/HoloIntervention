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
#include "LocatableCaptureDevice.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <mfmediacapture.h>
#include <collection.h>

using namespace Concurrency;
using namespace Windows::Media;
using namespace Windows::Media::Capture;
using namespace Windows::Storage;
using namespace Windows::Devices::Enumeration;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    LocatableCaptureDevice::LocatableCaptureDevice()
    {

    }

    //----------------------------------------------------------------------------
    task<void> LocatableCaptureDevice::InitializeAsync(IMFDXGIDeviceManager* pDxgiDeviceManager)
    {
      auto hr = MFStartup(MF_VERSION);
      if (FAILED(hr))
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Fatal error. No access to media subsystem.");
        return create_task([]() {});
      }

      try
      {
        auto mediaCapture = ref new Windows::Media::Capture::MediaCapture();
        m_mediaCapture = mediaCapture;
        m_mediaCapture->Failed += ref new MediaCaptureFailedEventHandler([](Windows::Media::Capture::MediaCapture ^ sender, MediaCaptureFailedEventArgs ^ args)
        {
          // Handle MediaCapture failure notification
          OutputDebugString(args->Message->Data());
        });

        auto initSetting = ref new MediaCaptureInitializationSettings;
        initSetting->StreamingCaptureMode = StreamingCaptureMode::AudioAndVideo;
        initSetting->MediaCategory = MediaCategory::Media;

        if (pDxgiDeviceManager)
        {
          // Optionally, you can put your D3D device into MediaCapture.
          // But, in most case, this is not mandatory.
          m_MFDXGIDeviceManager = pDxgiDeviceManager;
          Microsoft::WRL::ComPtr<IAdvancedMediaCaptureInitializationSettings> spAdvancedSettings;
          ((IUnknown*)initSetting)->QueryInterface(IID_PPV_ARGS(&spAdvancedSettings));
          spAdvancedSettings->SetDirectxDeviceManager(m_MFDXGIDeviceManager.Get());
        }

        return create_task(DeviceInformation::FindAllAsync(DeviceClass::VideoCapture)).then([this, initSetting](DeviceInformationCollection ^ collection)
        {
          if (collection->Size > 0)
          {
            initSetting->VideoDeviceId = collection->GetAt(0)->Id;
          }
          else
          {
            throw std::exception("No video devices for capture.");
          }

          return create_task(this->m_mediaCapture->InitializeAsync(initSetting)).then([this]()
          {
            m_initialized = true;
          });
        });
      }
      catch (Platform::Exception^ e)
      {
        DoCleanup();
        throw e;
      }

      return create_task([]() {});
    }

    //----------------------------------------------------------------------------
    void LocatableCaptureDevice::CleanupSink()
    {
      if (m_mediaSink)
      {
        delete m_mediaSink;
        m_mediaSink = nullptr;
        m_recordingStarted = false;
      }
    }

    //----------------------------------------------------------------------------
    void LocatableCaptureDevice::DoCleanup()
    {
      CleanupSink();
      auto hr = MFShutdown();
    }

    //----------------------------------------------------------------------------
    task<void> LocatableCaptureDevice::CleanupAsync()
    {
      Windows::Media::Capture::MediaCapture^ mediaCapture = m_mediaCapture.Get();
      if (mediaCapture == nullptr && !m_mediaSink)
      {
        return create_task([]() {});
      }

      if (mediaCapture != nullptr && m_recordingStarted)
      {
        return create_task(mediaCapture->StopRecordAsync()).then([this](task<void>&)
        {
          DoCleanup();
        });
      }
      else
      {
        DoCleanup();
      }
      return create_task([]() {});
    }

    //----------------------------------------------------------------------------
    task<void> LocatableCaptureDevice::SelectPreferredCameraStreamSettingAsync(MediaStreamType mediaStreamType, VideoSettingsFilter^ settingsFilterFunc)
    {
      auto preferredSettings = m_mediaCapture.Get()->VideoDeviceController->GetAvailableMediaStreamProperties(mediaStreamType);

      std::vector<Windows::Media::MediaProperties::IVideoEncodingProperties^> vector;
      for (unsigned int i = 0; i < preferredSettings->Size; i++)
      {
        auto prop = preferredSettings->GetAt(i);
        if (settingsFilterFunc(prop, i))
        {
          vector.push_back(static_cast<Windows::Media::MediaProperties::IVideoEncodingProperties^>(prop));
        }
      }

      std::sort(vector.begin(), vector.end(), [](Windows::Media::MediaProperties::IVideoEncodingProperties ^ prop1, Windows::Media::MediaProperties::IVideoEncodingProperties ^ prop2)
      {
        return (prop2->Width - prop1->Width);
      });

      if (vector.size() > 0)
      {
        return create_task(m_mediaCapture.Get()->VideoDeviceController->SetMediaStreamPropertiesAsync(mediaStreamType, vector.front()));
      }

      return create_task([]()
      {
        // Nothing to do
      });
    }

    //----------------------------------------------------------------------------
    task<void> LocatableCaptureDevice::StartRecordingAsync(Windows::Media::MediaProperties::MediaEncodingProfile^ mediaEncodingProfile)
    {
      // We cannot start recording twice.
      if (m_mediaSink && m_recordingStarted)
      {
        throw ref new Platform::Exception(__HRESULT_FROM_WIN32(ERROR_INVALID_STATE));
      }

      // Release sink if there is one already.
      CleanupSink();

      // Create new sink
      m_mediaSink = ref new HoloIntervention::MediaCapture::StspMediaSinkProxy();
      m_mediaSink->RegisterSampleCallback(ref new HoloIntervention::MediaCapture::SampleReceivedCallback([this](int* sample)
      {
        IMFSample* mfSample = (IMFSample*)(sample);

        std::lock_guard<std::mutex> guard(m_sampleAccess);
        m_samples.push_back(mfSample);
      }));

      return create_task(m_mediaSink->InitializeAsync(mediaEncodingProfile->Audio, mediaEncodingProfile->Video)).then([this, mediaEncodingProfile](Windows::Media::IMediaExtension ^ mediaExtension)
      {
        return create_task(m_mediaCapture->StartRecordToCustomSinkAsync(mediaEncodingProfile, mediaExtension)).then([this](task<void>& asyncInfo)
        {
          try
          {
            asyncInfo.get();
            m_recordingStarted = true;
          }
          catch (Platform::Exception^)
          {
            CleanupSink();
            throw;
          }
        });
      });
    }

    //----------------------------------------------------------------------------
    task<void> LocatableCaptureDevice::StopRecordingAsync()
    {
      if (m_recordingStarted)
      {
        return create_task(m_mediaCapture.Get()->StopRecordAsync()).then([this]()
        {
          CleanupSink();
        });
      }

      // If recording not started just do nothing
      return create_task([]() {});
    }

    //----------------------------------------------------------------------------
    bool LocatableCaptureDevice::Initialized::get()
    {
      return m_initialized;
    }
  }
}