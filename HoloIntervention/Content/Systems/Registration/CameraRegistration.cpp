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
#include <opencv2/calib3d.hpp>

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Devices::Core;
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
                m_cameraIntrinsics = m_videoFrameProcessor->TryGetCameraIntrinsics();
              }
            }).then([this]()
            {
              if (m_cameraIntrinsics == nullptr)
              {
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to retrieve camera intrinsics. Aborting.");
                m_videoFrameProcessor = nullptr;
              }
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
      cv::Mat l_mask;
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
                if (!results.empty() && ComputeCircleLocations(byteAccess, buffer, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, cameraResults))
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
        cv::Mat& mask,
        cv::Mat& cannyOutput,
        DetectedSphereWorldList& cameraResults)
    {
      assert(m_cameraIntrinsics != nullptr);

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
        mask = cv::Mat(desc.Height, desc.Width, CV_8UC3);
        cannyOutput = cv::Mat(desc.Height, desc.Width, CV_8UC1);

        initialized = true;
        height = desc.Height;
        width = desc.Width;
      }

      cv::Mat imageYUV(height + height / 2, width, CV_8UC1, (void*)data);
      cv::cvtColor(imageYUV, imageRGB, CV_YUV2RGB_NV12, 3);

      // Convert BGRA image to HSV image
      cv::cvtColor(imageRGB, hsv, cv::COLOR_RGB2HSV);

      // In HSV, red wraps around, check 0-10, 170-180
      // Filter everything except red - (0, 70, 50) -> (10, 255, 255) & (170, 70, 50) -> (180, 255, 255)
      cv::inRange(hsv, cv::Scalar(0, 70, 50), cv::Scalar(10, 255, 255), redMat);
      cv::inRange(hsv, cv::Scalar(170, 70, 50), cv::Scalar(180, 255, 255), redMatWrap);
      cv::addWeighted(redMat, 1.0, redMatWrap, 1.0, 0.0, mask);

      DetectedSpherePixelList spheres;
      std::vector<cv::Vec3f> circles;

      // Create a Gaussian & median Blur Filter
      cv::medianBlur(mask, mask, 5);
      cv::GaussianBlur(mask, mask, cv::Size(9, 9), 2, 2);

      // Apply the Hough Transform to find the circles
      cv::HoughCircles(mask, circles, CV_HOUGH_GRADIENT, 2, mask.rows / 16, 255, 30);
      if (circles.size() == 4)
      {
        for (auto& circle : circles)
        {
          spheres.push_back(cv::Point(cvRound(circle[0]), cvRound(circle[1])));
        }
      }
      else
      {
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i> hierarchy;

        int thresh = 100;
        int max_thresh = 255;

        // Blur the image
        cv::medianBlur(mask, mask, 3);

        // Detect edges using canny
        cv::Canny(mask, cannyOutput, thresh, thresh * 2, 3);

        // Find contours
        cv::findContours(cannyOutput, contours, hierarchy, CV_RETR_TREE, CV_CHAIN_APPROX_SIMPLE, cv::Point(0, 0));

        /// Approximate contours to polygons + get bounding rects and circles
        std::vector<std::vector<cv::Point> > contours_poly(contours.size());
        std::vector<cv::Point2f>center(contours.size());
        std::vector<float>radius(contours.size());

        for (uint32_t j = 0; j < contours.size(); j++)
        {
          cv::approxPolyDP(cv::Mat(contours[j]), contours_poly[j], 3, true);        // Finds polygon
          cv::minEnclosingCircle((cv::Mat)contours_poly[j], center[j], radius[j]);  // Finds circle
          spheres.push_back(DetectedSpherePixel(center[j]));
        }
      }

      if (spheres.size() != 4)
      {
        // Must have correspondence between image points and object points
        return false;
      }

      std::vector<float> distCoeffs;
      distCoeffs.push_back(m_cameraIntrinsics->RadialDistortion.x);
      distCoeffs.push_back(m_cameraIntrinsics->RadialDistortion.y);
      distCoeffs.push_back(m_cameraIntrinsics->TangentialDistortion.x);
      distCoeffs.push_back(m_cameraIntrinsics->TangentialDistortion.y);
      distCoeffs.push_back(m_cameraIntrinsics->RadialDistortion.z);

      Windows::Foundation::Numerics::float4x4& mat = transpose(m_cameraIntrinsics->UndistortedProjectionTransform);
      cv::Matx33f intrinsic;
      intrinsic(0, 0) = mat.m11;
      intrinsic(0, 1) = mat.m12;
      intrinsic(0, 2) = mat.m13;

      intrinsic(1, 0) = mat.m21;
      intrinsic(1, 1) = mat.m22;
      intrinsic(1, 2) = mat.m23;

      intrinsic(2, 0) = mat.m31;
      intrinsic(2, 1) = mat.m32;
      intrinsic(2, 2) = mat.m33;

      std::vector<float> rvec;
      std::vector<float> tvec;
      if (!cv::solvePnP(m_phantomFiducialCoords, spheres, intrinsic, distCoeffs, rvec, tvec))
      {
        OutputDebugStringW(L"Unable to solve object pose.");
        return false;
      }
      cv::Matx33f rotation;
      cv::Rodrigues(rvec, rotation);

      cv::Matx44f transform(cv::Matx44f::eye()); //identity
      transform(0, 0) = rotation(0, 0);
      transform(0, 1) = rotation(0, 1);
      transform(0, 2) = rotation(0, 2);

      transform(1, 0) = rotation(1, 0);
      transform(1, 1) = rotation(1, 1);
      transform(1, 2) = rotation(1, 2);

      transform(2, 0) = rotation(2, 0);
      transform(2, 1) = rotation(2, 1);
      transform(2, 2) = rotation(2, 2);
      transform(0, 3) = tvec[0];
      transform(1, 3) = tvec[1];
      transform(2, 3) = tvec[2];

      cameraResults.clear();
      int i = 0;
      for (auto& point : m_phantomFiducialCoords)
      {
        cv::Mat3f pointMat(point);
        cv::Mat3f resultMat;
        cv::multiply(transform, pointMat, resultMat);
        cameraResults.push_back(cv::Point3f(resultMat.at<float>(0), resultMat.at<float>(1), resultMat.at<float>(2)));
        i++;
      }

      // TODO : debug
      return true;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputeTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, CameraRegistration::DetectedSphereWorldList& worldResults)
    {
      m_transformRepository->SetTransforms(trackedFrame);

      // Calculate world position from transforms in tracked frame
      bool isValid(false);
      float4x4 red1ToPhantomTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red1Sphere", L"Phantom"), &isValid);
      float4x4 red1ToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red1Sphere", L"Reference"), &isValid);
      if (!isValid) {return false;}

      float4x4 red2ToPhantomTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red2Sphere", L"Phantom"), &isValid);
      float4x4 red2ToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red2Sphere", L"Reference"), &isValid);

      float4x4 red3ToPhantomTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red3Sphere", L"Phantom"), &isValid);
      float4x4 red3ToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red3Sphere", L"Reference"), &isValid);

      float4x4 red4ToPhantomTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red4Sphere", L"Phantom"), &isValid);
      float4x4 red4ToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red4Sphere", L"Reference"), &isValid);

      float3 scale;
      quaternion quat;
      float3 translation;
      bool result = decompose(transpose(red1ToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      result = decompose(transpose(red2ToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      result = decompose(transpose(red3ToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      result = decompose(transpose(red4ToReferenceTransform), &scale, &quat, &translation);
      if (!result) { return result; }
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      if (m_phantomFiducialCoords.empty())
      {
        // Phantom is rigid body, so only need to pull the values once
        // in order of enum
        std::vector<cv::Point3f> fiducialCoords;
        result = decompose(transpose(red1ToPhantomTransform), &scale, &quat, &translation);
        if (!result) { return result; }
        fiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        result = decompose(transpose(red2ToPhantomTransform), &scale, &quat, &translation);
        if (!result) { return result; }
        fiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        result = decompose(transpose(red3ToPhantomTransform), &scale, &quat, &translation);
        if (!result) { return result; }
        fiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        result = decompose(transpose(red4ToPhantomTransform), &scale, &quat, &translation);
        if (!result) { return result; }
        fiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        m_phantomFiducialCoords = fiducialCoords;
      }

      return true;
    }
  }
}