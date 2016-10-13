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
using namespace Windows::Media::Capture;

namespace HoloIntervention
{
  namespace Systems
  {
    class MediaCaptureManager
    {
      enum CaptureState
      {
        Unknown,
        Initialized,
        StartingRecord,
        Recording,
        StoppingRecord,
        TakingPhoto
      };

    public:
      MediaCaptureManager();
      virtual ~MediaCaptureManager();

      task<void> InitializeAsync(IMFDXGIDeviceManager* pDxgiDeviceManager = nullptr);

      task<void> StartRecordingAsync();
      task<void> StopRecordingAsync();

      task<void> TakePhotoAsync();
      bool CanTakePhoto();

    protected:
      Wrappers::SRWLock               m_lock;
      Platform::Agile<MediaCapture>   m_mediaCapture;

      CaptureState                    m_currentState;

      ComPtr<IMFDXGIDeviceManager>    m_spMFDXGIDeviceManager;
    };
  }
}