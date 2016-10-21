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
#include <ppltasks.h>
#include <ppl.h>

// stl includes
#include <algorithm>

// Network includes
#include "IGTLinkIF.h"

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
      cv::Mat l_hsv;
      cv::Mat l_redMat;
      cv::Mat l_redMatWrap;
      cv::Mat l_imageRGB;
      cv::Mat l_canny_output;
      std::mutex l_cannyLock;
      std::array<cv::Mat, 5> l_mask;
      bool l_initialized(false);
      int32_t l_height(0);
      int32_t l_width(0);
      UWPOpenIGTLink::TrackedFrame^ l_latestTrackedFrame(nullptr);
      MediaFrameReference^ l_latestCameraFrame(nullptr);

      while (!token.is_canceled())
      {
        if (m_videoFrameProcessor == nullptr || !HoloIntervention::instance()->GetIGTLink().IsConnected())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        UWPOpenIGTLink::TrackedFrame^ trackedFrame(nullptr);
        MediaFrameReference^ cameraFrame(m_videoFrameProcessor->GetLatestFrame());
        if (HoloIntervention::instance()->GetIGTLink().GetLatestTrackedFrame(l_latestTrackedFrame, &m_latestTimestamp) &&
            l_latestTrackedFrame != trackedFrame &&
            cameraFrame != nullptr &&
            cameraFrame != l_latestCameraFrame)
        {
          l_latestTrackedFrame = trackedFrame;
          l_latestCameraFrame = cameraFrame;
          if (m_worldCoordinateSystem != nullptr && l_latestCameraFrame->CoordinateSystem != nullptr)
          {
            try
            {
              Platform::IBox<float4x4>^ transformBox = l_latestCameraFrame->CoordinateSystem->TryGetTransformTo(m_worldCoordinateSystem);
              if (transformBox != nullptr)
              {
                m_cameraToWorld = transformBox->Value;
              }
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(L"Cannot retrieve camera to world transformation.");
              continue;
            }
          }

          DetectedSphereWorldList results;
          if (!ComputeTrackerFrameLocations(l_latestTrackedFrame, results)) {continue;}

          if (VideoMediaFrame^ videoMediaFrame = l_latestCameraFrame->VideoMediaFrame)
          {
            // Validate that the incoming frame format is compatible
            if (videoMediaFrame->SoftwareBitmap)
            {
              auto buffer = videoMediaFrame->SoftwareBitmap->LockBuffer(BitmapBufferAccessMode::Read);
              IMemoryBufferReference^ reference = buffer->CreateReference();

              Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> byteAccess;
              if (SUCCEEDED(reinterpret_cast<IUnknown*>(reference)->QueryInterface(IID_PPV_ARGS(&byteAccess))))
              {
                DetectedSphereWorldList cameraResults;
                if (!results.empty() && ComputeCircleLocations(byteAccess, buffer, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, l_cannyLock, cameraResults))
                {
                  m_cameraFrameResults.push_back(cameraResults);
                  m_trackerFrameResults.push_back(results);
                }
              }

              delete reference;
              delete buffer;
            }
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputeCircleLocations(Microsoft::WRL::ComPtr<Windows::Foundation::IMemoryBufferByteAccess>& byteAccess,
        Windows::Graphics::Imaging::BitmapBuffer^ buffer,
        bool& initialized,
        int32_t& height,
        int32_t& width,
        cv::Mat& hsv,
        cv::Mat& redMat,
        cv::Mat& redMatWrap,
        cv::Mat& imageRGB,
        std::array<cv::Mat, 5>& mask,
        cv::Mat& cannyOutput,
        std::mutex& cannyLock,
        DetectedSphereWorldList& cameraResults)
    {
      // Get a pointer to the pixel buffer
      byte* data;
      unsigned capacity;
      byteAccess->GetBuffer(&data, &capacity);

      // Get information about the BitmapBuffer
      auto desc = buffer->GetPlaneDescription(0);

      if (!initialized || height != desc.Height || width != desc.Width)
      {
        hsv = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        redMat = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        redMatWrap = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        imageRGB = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        mask[Red] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        mask[Green] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        mask[Blue] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        mask[Yellow] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        mask[Pink] = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        cannyOutput = cv::Mat(desc.Height, desc.Width, CV_8UC1);

        initialized = true;
        height = desc.Height;
        width = desc.Width;
      }

      cv::Mat imageYUV(height + height / 2, width, CV_8UC1, (void*)data);
      cv::cvtColor(imageYUV, imageRGB, CV_YUV2RGB_NV12, 3);

      // Convert BGRA image to HSV image
      cv::cvtColor(imageRGB, hsv, cv::COLOR_RGB2HSV);

      // This order must match enum order above
      std::array<task<cv::Mat*>, 5> maskTasks =
      {
        create_task([&]()
        {
          // In HSV, red wraps around, check 0-10, 170-180
          // Filter everything except red - (0, 70, 50) -> (10, 255, 255) & (170, 70, 50) -> (180, 255, 255)
          cv::inRange(hsv, cv::Scalar(0, 70, 50), cv::Scalar(10, 255, 255), redMat);
          cv::inRange(hsv, cv::Scalar(170, 70, 50), cv::Scalar(180, 255, 255), redMatWrap);
          cv::addWeighted(redMat, 1.0, redMatWrap, 1.0, 0.0, mask[Red]);
          return &mask[Red];
        }),
        create_task([&]()
        {
          // Filter everything except blue
          cv::inRange(hsv, cv::Scalar(100, 70, 50), cv::Scalar(120, 255, 255), mask[Blue]);
          return &mask[Blue];
        }),
        create_task([&]()
        {
          // Filter everything except green
          cv::inRange(hsv, cv::Scalar(65, 70, 50), cv::Scalar(85, 255, 255), mask[Green]);
          return &mask[Green];
        }),
        create_task([&]()
        {
          // Filter everything except yellow
          cv::inRange(hsv, cv::Scalar(15, 70, 50), cv::Scalar(35, 255, 255), mask[Yellow]);
          return &mask[Yellow];
        }),
        create_task([&]()
        {
          // Filter everything except pink
          cv::inRange(hsv, cv::Scalar(140, 70, 50), cv::Scalar(160, 255, 255), mask[Pink]);
          return &mask[Pink];
        })
      };

      std::array<task<void>, 5> resultTasks;
      DetectedSpherePixelList spheres;
      std::mutex sphereLock;
      for (int i = 0; i < 5; ++i)
      {
        resultTasks[i] = maskTasks[i].then([this, i, &cannyOutput, &spheres, &sphereLock, &cannyLock](cv::Mat * mask) -> void
        {
          std::vector<cv::Vec3f> circles;

          // Create a Gaussian & median Blur Filter
          cv::medianBlur(*mask, *mask, 5);
          cv::GaussianBlur(*mask, *mask, cv::Size(9, 9), 2, 2);

          // Apply the Hough Transform to find the circles
          cv::HoughCircles(*mask, circles, CV_HOUGH_GRADIENT, 2, mask->rows / 16, 255, 30);
          if (!circles.empty())
          {
            cv::Point center(cvRound(circles[0][0]), cvRound(circles[0][1]));

            std::lock_guard<std::mutex> guard(sphereLock);
            spheres.push_back(DetectedSpherePixel((SphereColour)i, center));
          }
          else
          {
            std::vector<std::vector<cv::Point>> contours;
            std::vector<cv::Vec4i> hierarchy;

            int thresh = 100;
            int max_thresh = 255;

            // Blur the image
            cv::medianBlur(*mask, *mask, 3);

            {
              std::lock_guard<std::mutex> guard(cannyLock);

              // Detect edges using canny
              cv::Canny(*mask, cannyOutput, thresh, thresh * 2, 3);

              // Find contours
              cv::findContours(cannyOutput, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));
            }

            /// Approximate contours to polygons + get bounding rects and circles
            std::vector<std::vector<cv::Point> > contours_poly(contours.size());
            std::vector<cv::Point2f>center(contours.size());
            std::vector<float>radius(contours.size());

            for (uint32_t j = 0; j < contours.size(); j++)
            {
              cv::approxPolyDP(cv::Mat(contours[j]), contours_poly[j], 3, true);        // Finds polygon
              cv::minEnclosingCircle((cv::Mat)contours_poly[j], center[j], radius[j]);  // Finds circle
            }

            std::lock_guard<std::mutex> guard(sphereLock);
            spheres.push_back(DetectedSpherePixel((SphereColour)i, center[0]));
          }
        });
      }

      auto joinTask = when_all(std::begin(resultTasks), std::end(resultTasks));
      joinTask.wait();

      DetectedSphereWorldList worldResults;
      if (spheres.size() < 3)
      {
        // Cannot compute world space with less than 3 detected spheres
        return false;
      }

      /*
      float2 ImagePosZeroToOne = float2( PixelPos.x / ImageWidth, 1.0 - (PixelPos.y / ImageHeight ) );
      float2 ImagePosProjected = ( ( ImagePosZeroToOne * 2.0 ) - float2(1,1) ); // -1 to 1 space
      float3 CameraSpacePos = UnProjectVector( Projection, float3( ImagePosProjected, 1) );
      float3 WorldSpaceRayPoint1 = mul( CameraToWorld, float4(0,0,0,1) ); // camera location in world space
      float3 WorldSpaceRayPoint2 = mul( CameraToWorld, CameraSpacePos ); // ray point in world space

      public static Vector3 UnProjectVector(Matrix4x4 proj, Vector3 to)
      {
      Vector3 from = new Vector3(0, 0, 0);
      var axsX = proj.GetRow(0);
      var axsY = proj.GetRow(1);
      var axsZ = proj.GetRow(2);
      from.z = to.z / axsZ.z;
      from.y = (to.y - (from.z * axsY.z)) / axsY.y;
      from.x = (to.x - (from.z * axsX.z)) / axsX.x;
      return from;
      }
      */

      // TODO : calculate world position from pixel locations
      return true;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputeTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, CameraRegistration::DetectedSphereWorldList& worldResults)
    {
      m_transformRepository->SetTransforms(trackedFrame);

      // Calculate world position from transforms in tracked frame
      bool isValid(false);
      float4x4 redToGreenTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere", L"GreenSphere"), &isValid);
      float4x4 redToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere", L"Reference"), &isValid);
      if (!isValid) {return false;}

      float4x4 greenToPinkTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"GreenSphere", L"PinkSphere"), &isValid);
      float4x4 greenToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"GreenSphere", L"Reference"), &isValid);

      float4x4 pinkToBlueTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"PinkSphere", L"BlueSphere"), &isValid);
      float4x4 pinkToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"PinkSphere", L"Reference"), &isValid);

      float4x4 blueToYellowTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"BlueSphere", L"YellowSphere"), &isValid);
      float4x4 blueToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"BlueSphere", L"Reference"), &isValid);

      float4x4 yellowToRedTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"YellowSphere", L"RedSphere"), &isValid);
      float4x4 yellowToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"YellowSphere", L"Reference"), &isValid);

      float3 scale;
      quaternion quat;
      float3 translation;
      bool result = decompose(transpose(redToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(Red, cv::Point3f(translation.x, translation.y, translation.z)));

      result = decompose(transpose(greenToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(Green, cv::Point3f(translation.x, translation.y, translation.z)));

      result = decompose(transpose(pinkToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(Pink, cv::Point3f(translation.x, translation.y, translation.z)));

      result = decompose(transpose(blueToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(Blue, cv::Point3f(translation.x, translation.y, translation.z)));

      result = decompose(transpose(yellowToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(Yellow, cv::Point3f(translation.x, translation.y, translation.z)));

      return true;
    }

  }
}