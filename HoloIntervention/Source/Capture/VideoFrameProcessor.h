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

// STL includes
#include <mutex>

namespace HoloIntervention
{
  namespace Capture
  {
    // Class to manage receiving video frames from Windows::Media::Capture
    class VideoFrameProcessor
    {
    public:
      static Concurrency::task<std::shared_ptr<VideoFrameProcessor>> CreateAsync(Windows::Media::Capture::Frames::MediaFrameSourceInfo^ details = nullptr, Windows::Media::Capture::MediaCaptureInitializationSettings^ settings = nullptr);

      VideoFrameProcessor(
        Platform::Agile<Windows::Media::Capture::MediaCapture> mediaCapture,
        Windows::Media::Capture::Frames::MediaFrameReader^ reader,
        Windows::Media::Capture::Frames::MediaFrameSource^ source);

      Windows::Media::Capture::Frames::MediaFrameReference^ GetLatestFrame(void) const;
      Windows::Media::Capture::Frames::VideoMediaFrameFormat^ GetCurrentFormat(void) const;

      Concurrency::task<void> StopAsync();
      Concurrency::task<Windows::Media::Capture::Frames::MediaFrameReaderStartStatus> StartAsync();

      bool IsStarted();

    protected:
      void OnFrameArrived(Windows::Media::Capture::Frames::MediaFrameReader^ sender, Windows::Media::Capture::Frames::MediaFrameArrivedEventArgs^ args);

      Platform::Agile<Windows::Media::Capture::MediaCapture>              m_mediaCapture;
      Windows::Media::Capture::Frames::MediaFrameReader^                  m_mediaFrameReader;

      mutable std::mutex                                                  m_propertiesLock;
      Windows::Media::Capture::Frames::MediaFrameSource^                  m_mediaFrameSource;
      Windows::Media::Capture::Frames::MediaFrameReference^               m_latestFrame = nullptr;
      bool                                                                m_recording = false;
    };
  }
}