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
#include "VideoFrameProcessor.h"

// Common includes
#include "DeviceResources.h"

// System includes
#include "NotificationSystem.h"

// WinRT includes
#include <MemoryBuffer.h>
#include <ppltasks.h>
#include <ppl.h>

// stl includes
#include <algorithm>

// OpenCV includes
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

using namespace Concurrency;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::MediaProperties;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    CameraRegistration::CameraRegistration(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {

    }

    //----------------------------------------------------------------------------
    CameraRegistration::~CameraRegistration()
    {

    }

    //----------------------------------------------------------------------------
    void CameraRegistration::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      m_worldCoordinateSystem = coordinateSystem;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::RegisterVoiceCallbacks(Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"start camera"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_videoFrameProcessor == nullptr)
        {
          std::lock_guard<std::mutex> guard(m_processorLock);
          if (m_createTask == nullptr)
          {
            m_createTask = &Capture::VideoFrameProcessor::CreateAsync();
            m_createTask->then([this](std::shared_ptr<Capture::VideoFrameProcessor> processor)
            {
              std::lock_guard<std::mutex> guard(m_processorLock);
              if (processor == nullptr)
              {
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to initialize capture system.");
              }
              else
              {
                m_videoFrameProcessor = processor;
              }
            }).then([this]()
            {
              std::lock_guard<std::mutex> guard(m_processorLock);
              m_videoFrameProcessor->StartAsync().then([this](MediaFrameReaderStartStatus status)
              {
                if (status == MediaFrameReaderStartStatus::Success)
                {
                  HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing...");
                }
                else
                {
                  HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to start capturing.");
                }
              });
            });
          }
        }
        else
        {
          m_videoFrameProcessor->StartAsync().then([this](MediaFrameReaderStartStatus status)
          {
            if (status == MediaFrameReaderStartStatus::Success)
            {
              HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing...");
            }
            else
            {
              HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to start capturing.");
            }
          });
        }

        if (m_workerTask == nullptr)
        {
          m_tokenSource = cancellation_token_source();
          auto token = m_tokenSource.get_token();
          m_workerTask = &concurrency::create_task([this, token]()
          {
            ProcessAvailableFrames(token);
          }).then([this]()
          {
            m_workerTask = nullptr;
          });
        }
      };

      callbackMap[L"stop camera"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_videoFrameProcessor != nullptr && m_videoFrameProcessor->IsStarted())
        {
          m_tokenSource.cancel();
          std::lock_guard<std::mutex> guard(m_processorLock);
          m_videoFrameProcessor->StopAsync().then([this]()
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing stopped.");
          });
        }
      };
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::ProcessAvailableFrames(cancellation_token token)
    {
      enum SphereColour
      {
        Red,
        Blue,
        Green,
        Black,
        Gray
      };
      std::vector<std::pair<SphereColour, cv::Point2f>> poseCenters;
      cv::Mat redMat;
      cv::Mat hsv;
      cv::Mat redMatWrap;
      cv::Mat blueMat;
      cv::Mat greenMat;
      cv::Mat blackMat;
      cv::Mat imageRGB;
      std::array<cv::Mat, 5> mask;
      bool l_initialized(false);
      int32_t l_height(0);
      int32_t l_width(0);
      std::mutex l_lockAccess;

      while (!token.is_canceled())
      {
        if (m_videoFrameProcessor == nullptr)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        if (auto frame = m_videoFrameProcessor->GetLatestFrame())
        {
          if (m_worldCoordinateSystem != nullptr && frame->CoordinateSystem != nullptr)
          {
            try
            {
              Platform::IBox<float4x4>^ transformBox = frame->CoordinateSystem->TryGetTransformTo(m_worldCoordinateSystem);
              if (transformBox != nullptr)
              {
                m_cameraToWorld = transformBox->Value;
              }
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(L"Cannot retrieve camera to world transformation.");
            }
          }

          if (VideoMediaFrame^ videoMediaFrame = frame->VideoMediaFrame)
          {
            // Validate that the incoming frame format is compatible
            if (videoMediaFrame->SoftwareBitmap)
            {
              auto buffer = videoMediaFrame->SoftwareBitmap->LockBuffer(BitmapBufferAccessMode::Read);
              IMemoryBufferReference^ reference = buffer->CreateReference();

              Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> byteAccess;
              if (SUCCEEDED(reinterpret_cast<IUnknown*>(reference)->QueryInterface(IID_PPV_ARGS(&byteAccess))))
              {
                poseCenters.clear();

                // Get a pointer to the pixel buffer
                byte* data;
                unsigned capacity;
                byteAccess->GetBuffer(&data, &capacity);

                // Get information about the BitmapBuffer
                auto desc = buffer->GetPlaneDescription(0);

                if (!l_initialized || l_height != desc.Height || l_width != desc.Width)
                {
                  redMatWrap = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  hsv = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  redMat = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  imageRGB = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  mask[0] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  mask[1] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  mask[2] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  mask[3] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  mask[4] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
                  l_initialized = true;
                  l_height = desc.Height;
                  l_width = desc.Width;
                }

                cv::Mat imageYUV(l_height + l_height / 2, l_width, CV_8UC1, (void*)data);
                cv::cvtColor(imageYUV, imageRGB, CV_YUV2RGB_NV12, 3);

                // Convert BGRA image to HSV image
                cv::cvtColor(imageRGB, hsv, cv::COLOR_RGB2HSV);

                auto redTask = create_task([&]()
                {
                  // Filter everything except red - (0, 70, 50) -> (10, 255, 255) & (160, 70, 50) -> (179, 255, 255)
                  cv::inRange(hsv, cv::Scalar(0, 70, 50), cv::Scalar(10, 255, 255), redMat);
                  cv::inRange(hsv, cv::Scalar(160, 70, 50), cv::Scalar(179, 255, 255), redMatWrap);
                  cv::addWeighted(redMat, 1.0, redMatWrap, 1.0, 0.0, mask[0]);
                  return mask[0];
                });
                auto blueTask = create_task([&]()
                {
                  // Filter everything except blue -
                  cv::inRange(hsv, cv::Scalar(110, 50, 50), cv::Scalar(130, 255, 255), mask[1]);
                  return mask[1];
                });
                auto greenTask = create_task([&]()
                {
                  // Filter everything except blue -
                  cv::inRange(hsv, cv::Scalar(50, 50, 50), cv::Scalar(70, 255, 255), mask[2]);
                  return mask[2];
                });
                // This order must match enum order above
                std::array<task<cv::Mat>*, 3> tasks = { &redTask, &blueTask, &greenTask };
                std::vector<task<void>> resultTasks;
                for (int i = 0; i < 3; ++i)
                {
                  resultTasks.push_back(tasks[i]->then([this, i, &poseCenters, &l_lockAccess](cv::Mat mask) -> void
                  {
                    std::vector<cv::Vec3f> circles;

                    // Create a Gaussian & median Blur Filter
                    cv::medianBlur(mask, mask, 5);
                    cv::GaussianBlur(mask, mask, cv::Size(9, 9), 2, 2);

                    // Apply the Hough Transform to find the circles
                    cv::HoughCircles(mask, circles, CV_HOUGH_GRADIENT, 2, mask.rows / 16, 255, 30);
                    if (circles.size() > 0)
                    {
                      cv::Point center(cvRound(circles[0][0]), cvRound(circles[0][1]));

                      std::lock_guard<std::mutex> guard(l_lockAccess);
                      poseCenters.push_back(std::pair<SphereColour, cv::Point2f>((SphereColour)i, center));
                    }
                    else
                    {
                      std::vector<std::vector<cv::Point>> contours;
                      std::vector<cv::Vec4i> hierarchy;
                      cv::Mat canny_output;

                      int thresh = 100;
                      int max_thresh = 255;

                      // Blur the image
                      cv::medianBlur(mask, mask, 3);

                      // Detect edges using canny
                      cv::Canny(mask, canny_output, thresh, thresh * 2, 3);

                      // Find contours
                      cv::findContours(canny_output, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));

                      /// Approximate contours to polygons + get bounding rects and circles
                      std::vector<std::vector<cv::Point> > contours_poly(contours.size());
                      std::vector<cv::Point2f>center(contours.size());
                      std::vector<float>radius(contours.size());

                      for (uint32_t j = 0; j < contours.size(); j++)
                      {
                        cv::approxPolyDP(cv::Mat(contours[j]), contours_poly[j], 3, true);        // Finds polygon
                        cv::minEnclosingCircle((cv::Mat)contours_poly[j], center[j], radius[j]);  // Finds circle
                      }

                      std::lock_guard<std::mutex> guard(l_lockAccess);
                      poseCenters.push_back(std::pair<SphereColour, cv::Point2f>((SphereColour)i, center[0]));
                    }
                  }));
                }

                auto joinTask = when_all(std::begin(resultTasks), std::end(resultTasks));
                joinTask.wait();
              }

              delete reference;
              delete buffer;
            }
          }
        }
      }
    }

  }
}