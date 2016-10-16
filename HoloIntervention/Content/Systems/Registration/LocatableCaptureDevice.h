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

#pragma once

// WinRT includes
#include <ppltasks.h>
#include <mfapi.h>

using namespace Concurrency;
using namespace Microsoft::WRL;
using namespace Windows::Media;

namespace HoloIntervention
{
  namespace System
  {
    delegate bool VideoSettingsFilter(Windows::Media::MediaProperties::IMediaEncodingProperties^, unsigned int);

    // Capture device represents a device used in one capture session between
    // calling CaptureManager.lockAsync and CaptureManager.unlockAsync.
    ref class LocatableCaptureDevice sealed
    {
    public:
      property bool Initialized { bool get(); }

    internal:
      LocatableCaptureDevice();

      task<void> InitializeAsync(IMFDXGIDeviceManager* pDxgiDeviceManager);
      void CleanupSink();
      void DoCleanup();
      task<void> CleanupAsync();
      task<void> SelectPreferredCameraStreamSettingAsync(Windows::Media::Capture::MediaStreamType mediaStreamType, VideoSettingsFilter^ settingsFilterFunc);
      task<void> StartRecordingAsync(Windows::Media::MediaProperties::MediaEncodingProfile^ mediaEncodingProfile);
      task<void> StopRecordingAsync();

      property Platform::Agile<Windows::Media::Capture::MediaCapture> MediaCapture
      {
        Platform::Agile<Windows::Media::Capture::MediaCapture> get();
      }

    private:
      // Media capture object
      Platform::Agile<Windows::Media::Capture::MediaCapture>  m_mediaCapture = nullptr;
      bool                                                    m_initialized = false;
      HoloIntervention::MediaCapture::StspMediaSinkProxy^     m_mediaSink = nullptr;
      Microsoft::WRL::ComPtr<IMFDXGIDeviceManager>            m_MFDXGIDeviceManager;
      bool                                                    m_recordingStarted = false;

      std::mutex                                              m_sampleAccess;
      std::vector<IMFSample*>                                 m_samples;
    };
  }
}