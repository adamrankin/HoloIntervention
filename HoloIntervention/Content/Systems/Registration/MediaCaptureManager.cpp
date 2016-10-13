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
#include "MediaCaptureManager.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <mfmediacapture.h>
#include <collection.h>

using namespace Concurrency;
using namespace Windows::Media;
using namespace Windows::Media::Capture;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace Systems
  {
    //----------------------------------------------------------------------------
    MediaCaptureManager::MediaCaptureManager()
      : m_currentState(Unknown)
    {
    }

    //----------------------------------------------------------------------------
    MediaCaptureManager::~MediaCaptureManager()
    {
    }

    //----------------------------------------------------------------------------
    task<void> MediaCaptureManager::InitializeAsync(IMFDXGIDeviceManager* pDxgiDeviceManager)
    {
      m_mediaCapture = ref new MediaCapture;

      m_mediaCapture->Failed += ref new MediaCaptureFailedEventHandler([](MediaCapture ^ sender, MediaCaptureFailedEventArgs ^ args)
      {
        // Handle MediaCapture failure notification
        OutputDebugStringW(args->Message->Data());
      });

      auto initSetting = ref new Windows::Media::Capture::MediaCaptureInitializationSettings;
      initSetting->StreamingCaptureMode = Windows::Media::Capture::StreamingCaptureMode::AudioAndVideo;
      initSetting->MediaCategory = Windows::Media::Capture::MediaCategory::Media;

      if (pDxgiDeviceManager)
      {
        // Optionally, you can put your D3D device into MediaCapture.
        // But, in most case, this is not mandatory.
        m_spMFDXGIDeviceManager = pDxgiDeviceManager;
        Microsoft::WRL::ComPtr<IAdvancedMediaCaptureInitializationSettings> spAdvancedSettings;
        ((IUnknown*)initSetting)->QueryInterface(IID_PPV_ARGS(&spAdvancedSettings));
        spAdvancedSettings->SetDirectxDeviceManager(m_spMFDXGIDeviceManager.Get());
      }

      return create_task(m_mediaCapture->InitializeAsync(initSetting)).then([this]()
      {
        auto lock = m_lock.LockExclusive();
        m_currentState = Initialized;
      });
    }

    //----------------------------------------------------------------------------
    task<void> MediaCaptureManager::StartRecordingAsync()
    {
      // Lock to check current state
      auto lock = m_lock.LockExclusive();

      if (m_currentState != Initialized)
      {
        throw ref new Platform::FailureException(L"Trying to start recording in invalid state.");
      }

      m_currentState = StartingRecord;

      // Start video recording without any effects
      auto encodingProperties = MediaProperties::MediaEncodingProfile::CreateAvi(MediaProperties::VideoEncodingQuality::Auto);

      // TODO : convert this to capture to custom sink
      return create_task([this]()
      {

      });
      /*
      return create_task(m_mediaCapture->StartRecordToCustomSinkAsync(encodingProperties, saveFile)).then([this]()
      {
        auto lock = m_lock.LockExclusive();
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Recording started.");
        m_currentState = Recording;
      });
      */
    }

    //----------------------------------------------------------------------------
    task<void> MediaCaptureManager::StopRecordingAsync()
    {
      // Lock to check current state
      auto lock = m_lock.LockExclusive();

      if (m_currentState != Recording)
      {
        throw ref new Platform::FailureException(L"Trying to stop recording in invalid state.");
      }

      m_currentState = StoppingRecord;

      // Stop video recording
      return create_task(m_mediaCapture->StopRecordAsync()).then([this]()
      {
        auto lock = m_lock.LockExclusive();
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Recording stopped.");
        m_currentState = Initialized;
      });
    }

    //----------------------------------------------------------------------------
    task<void> MediaCaptureManager::TakePhotoAsync()
    {
      // Lock to check current state
      auto lock = m_lock.LockExclusive();

      if (m_currentState != Initialized)
      {
        throw ref new Platform::FailureException();
      }

      m_currentState = TakingPhoto;

      return create_task(Windows::Storage::KnownFolders::GetFolderForUserAsync(nullptr, Windows::Storage::KnownFolderId::CameraRoll)).then([this](Windows::Storage::StorageFolder ^ folder)
      {
        if (!folder)
        {
          throw ref new Platform::FailureException(L"Can't find camera roll folder for this user");
        }

        // Create storage file for picture
        return create_task(folder->CreateFileAsync(L"MRCPhoto.jpg", Windows::Storage::CreationCollisionOption::GenerateUniqueName)).then([this](Windows::Storage::StorageFile ^ saveFile)
        {
          if (!saveFile)
          {
            throw ref new Platform::FailureException(L"Can't open file for capture.");
          }

          auto lock = m_lock.LockShared();

          // Taking picture without any effect
          auto encodingProperties = MediaProperties::ImageEncodingProperties::CreateJpeg();

          return create_task(m_mediaCapture->CapturePhotoToStorageFileAsync(encodingProperties, saveFile)).then([this]()
          {
            auto lock = m_lock.LockExclusive();
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Photo captured.");
            m_currentState = Initialized;
          });
        });
      });
    }

    //----------------------------------------------------------------------------
    bool MediaCaptureManager::CanTakePhoto()
    {
      auto lock = m_lock.LockShared();
      return m_currentState == Initialized;
    }
  }
}