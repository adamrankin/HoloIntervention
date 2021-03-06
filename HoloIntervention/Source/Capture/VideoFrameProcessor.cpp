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

// Local includes
#include "pch.h"
#include "VideoFrameProcessor.h"

using namespace Concurrency;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Capture;
using namespace Windows::Media::Devices::Core;
using namespace std::placeholders;

namespace HoloIntervention
{
  namespace Capture
  {
    //----------------------------------------------------------------------------
    VideoFrameProcessor::VideoFrameProcessor(Platform::Agile<MediaCapture> mediaCapture, MediaFrameReader^ reader, MediaFrameSource^ source)
      : m_mediaCapture(std::move(mediaCapture))
      , m_mediaFrameReader(std::move(reader))
      , m_mediaFrameSource(std::move(source))
    {
      // Listen for new frames, so we know when to update our m_latestFrame
      m_mediaFrameReader->FrameArrived +=
        ref new TypedEventHandler<MediaFrameReader^, MediaFrameArrivedEventArgs^>(
          std::bind(&VideoFrameProcessor::OnFrameArrived, this, _1, _2));
    }

    //----------------------------------------------------------------------------
    task<std::shared_ptr<VideoFrameProcessor>> VideoFrameProcessor::CreateAsync(MediaFrameSourceInfo^ details, MediaCaptureInitializationSettings^ settings)
    {
      return create_task(MediaFrameSourceGroup::FindAllAsync()).then([details, &settings](IVectorView<MediaFrameSourceGroup^>^ groups)
      {
        MediaFrameSourceGroup^ selectedGroup = nullptr;
        MediaFrameSourceInfo^ selectedSourceInfo = nullptr;
        MediaStreamType type;
        MediaFrameSourceKind kind;
        if (details != nullptr)
        {
          type = details->MediaStreamType;
          kind = details->SourceKind;
        }
        else
        {
          type = MediaStreamType::VideoRecord;
          kind = MediaFrameSourceKind::Color;
        }

        // Pick first color source.
        for (MediaFrameSourceGroup^ sourceGroup : groups)
        {
          for (MediaFrameSourceInfo^ sourceInfo : sourceGroup->SourceInfos)
          {
            if (sourceInfo->MediaStreamType == type && sourceInfo->SourceKind == kind)
            {
              selectedSourceInfo = sourceInfo;
              break;
            }
          }

          if (selectedSourceInfo != nullptr)
          {
            selectedGroup = sourceGroup;
            break;
          }
        }

        // No valid camera was found. This will happen on the emulator.
        if (selectedGroup == nullptr || selectedSourceInfo == nullptr)
        {
          return task_from_result(std::shared_ptr<VideoFrameProcessor>(nullptr));
        }

        if (settings == nullptr)
        {
          settings = ref new MediaCaptureInitializationSettings();
          settings->MemoryPreference = MediaCaptureMemoryPreference::Cpu; // Need SoftwareBitmaps
          settings->StreamingCaptureMode = StreamingCaptureMode::Video;   // Only need to stream video
          settings->SourceGroup = selectedGroup;
        }

        Platform::Agile<MediaCapture> mediaCapture(ref new MediaCapture());

        return create_task(mediaCapture->InitializeAsync(settings)).then([ = ](task<void> previousTask)
        {
          try
          {
            previousTask.wait();
          }
          catch (const std::exception& e)
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to initialize media capture: ") + e.what());
            return task_from_result(std::shared_ptr<VideoFrameProcessor>(nullptr));
          }

          MediaFrameSource^ selectedSource = mediaCapture->FrameSources->Lookup(selectedSourceInfo->Id);
          auto formats = selectedSource->SupportedFormats;

          return create_task(mediaCapture->CreateFrameReaderAsync(selectedSource)).then([ = ](MediaFrameReader ^ reader)
          {
            return std::make_shared<VideoFrameProcessor>(mediaCapture, reader, selectedSource);
          });
        });
      });
    }

    //----------------------------------------------------------------------------
    MediaFrameReference^ VideoFrameProcessor::GetLatestFrame(void) const
    {
      std::lock_guard<std::mutex> guard(m_propertiesLock);
      return m_latestFrame;
    }

    //----------------------------------------------------------------------------
    VideoMediaFrameFormat^ VideoFrameProcessor::GetCurrentFormat(void) const
    {
      return m_mediaFrameSource->CurrentFormat->VideoFormat;
    }

    //----------------------------------------------------------------------------
    task<void> VideoFrameProcessor::StopAsync()
    {
      return create_task(m_mediaFrameReader->StopAsync()).then([this]()
      {
        m_recording = false;
      });
    }

    //----------------------------------------------------------------------------
    task<MediaFrameReaderStartStatus> VideoFrameProcessor::StartAsync()
    {
      auto task = create_task(m_mediaFrameReader->StartAsync());
      task.then([this](MediaFrameReaderStartStatus status)
      {
        if (status == MediaFrameReaderStartStatus::Success)
        {
          m_recording = true;
        }
      });
      return task;
    }

    //----------------------------------------------------------------------------
    bool VideoFrameProcessor::IsStarted()
    {
      return m_recording;
    }

    //----------------------------------------------------------------------------
    void VideoFrameProcessor::OnFrameArrived(MediaFrameReader^ sender, MediaFrameArrivedEventArgs^ args)
    {
      if (MediaFrameReference^ frame = sender->TryAcquireLatestFrame())
      {
        std::lock_guard<std::mutex> guard(m_propertiesLock);
        m_latestFrame = frame;
      }
    }
  }
}