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

// STL includes
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
        StartCameraAsync();
        return;
      };

      callbackMap[L"stop camera"] = [this](SpeechRecognitionResult ^ result)
      {
        StopCameraAsync();

      };
    }

    //----------------------------------------------------------------------------
    task<void> CameraRegistration::StopCameraAsync()
    {
      if (m_videoFrameProcessor != nullptr && m_videoFrameProcessor->IsStarted())
      {
        m_tokenSource.cancel();
        std::lock_guard<std::mutex> guard(m_processorLock);
        return m_videoFrameProcessor->StopAsync().then([this]()
        {
          m_videoFrameProcessor = nullptr;
          m_createTask = nullptr;
          m_cameraFrameResults.clear();
          m_currentFrame = nullptr;
          m_nextFrame = nullptr;
          m_workerTask = nullptr;
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing stopped.");
        });
      }
      return create_task([]() {});
    }

    //----------------------------------------------------------------------------
    task<void> CameraRegistration::StartCameraAsync()
    {
      return StopCameraAsync().then([this]()
      {
        std::lock_guard<std::mutex> guard(m_processorLock);
        if (m_videoFrameProcessor == nullptr)
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
            return m_videoFrameProcessor->StartAsync().then([this](MediaFrameReaderStartStatus status)
            {
              if (status == MediaFrameReaderStartStatus::Success)
              {
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing...");
              }
              else
              {
                HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to start capturing.");
              }
              m_createTask = nullptr;
            }).then([this](task<void> previousTask)
            {
              if (m_videoFrameProcessor != nullptr)
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
            });
          });
        }
      });
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
        if (m_videoFrameProcessor == nullptr) //|| !HoloIntervention::instance()->GetIGTLink().IsConnected())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        // REMOVE ONCE WORKING
        MediaFrameReference^ cameraFrame2(m_videoFrameProcessor->GetLatestFrame());
        if (cameraFrame2 == nullptr)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        DetectedSpheresWorld cameraResults;
        if (ComputeCircleLocations(cameraFrame2->VideoMediaFrame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, cameraResults))
        {
          m_cameraFrameResults.push_back(cameraResults);
        }
        else
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        // END REMOVE ONCE WORKING

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

          DetectedSpheresWorld results;
          if (!ComputeTrackerFrameLocations(l_latestTrackedFrame, results))
          {
            continue;
          }

          DetectedSpheresWorld cameraResults;
          if (!results.empty() && ComputeCircleLocations(l_latestCameraFrame->VideoMediaFrame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, cameraResults))
          {
            m_cameraFrameResults.push_back(cameraResults);
            m_trackerFrameResults.push_back(results);
          }
        }
        else
        {
          // No new frame
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
      }
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputeCircleLocations(VideoMediaFrame^ videoFrame,
        bool& initialized,
        int32_t& height,
        int32_t& width,
        cv::Mat& hsv,
        cv::Mat& redMat,
        cv::Mat& redMatWrap,
        cv::Mat& imageRGB,
        cv::Mat& mask,
        cv::Mat& cannyOutput,
        DetectedSpheresWorld& cameraResults)
    {
      if (m_phantomFiducialCoords.size() != PHANTOM_SPHERE_COUNT)
      {
        OutputDebugStringW(L"Phantom coordinates haven't been received. Can't determine 3D sphere coordinates.");
        return false;
      }

      CameraIntrinsics^ cameraIntrinsics(nullptr);
      try
      {
        if (videoFrame == nullptr || videoFrame->CameraIntrinsics == nullptr)
        {
          OutputDebugStringW(L"Camera intrinsics not available. Cannot continue.");
          return false;
        }
        cameraIntrinsics = videoFrame->CameraIntrinsics;
      }
      catch (Platform::Exception^ e)
      {
        OutputDebugStringW(e->Message->Data());
      }

      bool result(false);
      // Validate that the incoming frame format is compatible
      if (videoFrame->SoftwareBitmap)
      {
        ComPtr<IMemoryBufferByteAccess> byteAccess;
        BitmapBuffer^ buffer = videoFrame->SoftwareBitmap->LockBuffer(BitmapBufferAccessMode::Read);
        IMemoryBufferReference^ reference = buffer->CreateReference();

        if (SUCCEEDED(reinterpret_cast<IUnknown*>(reference)->QueryInterface(IID_PPV_ARGS(&byteAccess))))
        {
          // Get a pointer to the pixel buffer
          byte* data;
          unsigned capacity;
          byteAccess->GetBuffer(&data, &capacity);

          // Get information about the BitmapBuffer
          auto desc = buffer->GetPlaneDescription(0);

          if (!initialized || height != desc.Height || width != desc.Width)
          {
            initialized = true;
            height = desc.Height;
            width = desc.Width;
          }

          cv::Mat imageYUV(height + height / 2, width, CV_8UC1, (void*)data);
          cv::cvtColor(imageYUV, imageRGB, cv::COLOR_YUV2RGB_NV12, 3);

          // Convert BGRA image to HSV image
          cv::cvtColor(imageRGB, hsv, cv::COLOR_RGB2HSV);

          // TODO : there is a way to do this in a single call, optimize
          // In HSV, red wraps around, check 0-10, 170-180
          cv::inRange(hsv, cv::Scalar(0, 70, 50), cv::Scalar(10, 255, 255), redMat);
          cv::inRange(hsv, cv::Scalar(170, 70, 50), cv::Scalar(180, 255, 255), redMatWrap);
          cv::addWeighted(redMat, 1.0, redMatWrap, 1.0, 0.0, mask);

          DetectedSpheresPixel spheres;
          std::vector<cv::Vec3f> circles;

          // Create a Gaussian & median Blur Filter
          cv::medianBlur(mask, mask, 5);
          cv::GaussianBlur(mask, mask, cv::Size(9, 9), 2, 2);

          // Apply the Hough Transform to find the circles
          try
          {
            cv::HoughCircles(mask, circles, CV_HOUGH_GRADIENT, 2, mask.rows / 16, 255, 30, 30, 60);
          }
          catch (const cv::Exception& e)
          {
            OutputDebugStringA(e.msg.c_str());
            result = false;
            goto done;
          }

          if (circles.size() == PHANTOM_SPHERE_COUNT)
          {
            float radiusMean(0.f);
            for (auto& circle : circles)
            {
              radiusMean += circle[2];
            }
            radiusMean /= circles.size();

            for (auto& circle : circles)
            {
              // Ensure radius of circle falls within 10% of mean
              if (circle[2] / radiusMean < 0.9f || circle[2] / radiusMean > 1.1f)
              {
                OutputDebugStringW(L"Circle detection failed. Irregular sized circle detected.");
                result = false;
                goto done;
              }

              // Outline circle and centroid
              cv::Point2f centerHough(circle[0], circle[1]);
              spheres.push_back(centerHough);
            }
          }
          else
          {
            OutputDebugStringW(L"Circle detection failed. Did not detect correct number of spheres.");
            result = false;
            goto done;
          }

          std::vector<float> distCoeffs;
          distCoeffs.push_back(cameraIntrinsics->RadialDistortion.x);
          distCoeffs.push_back(cameraIntrinsics->RadialDistortion.y);
          distCoeffs.push_back(cameraIntrinsics->TangentialDistortion.x);
          distCoeffs.push_back(cameraIntrinsics->TangentialDistortion.y);
          distCoeffs.push_back(cameraIntrinsics->RadialDistortion.z);

          cv::Matx33f intrinsic(cv::Matx33f::eye());
          intrinsic(0, 0) = cameraIntrinsics->FocalLength.x;
          intrinsic(0, 2) = cameraIntrinsics->PrincipalPoint.x;
          intrinsic(1, 1) = cameraIntrinsics->FocalLength.y;
          intrinsic(1, 2) = cameraIntrinsics->PrincipalPoint.y;

          std::vector<float> rvec;
          std::vector<float> tvec;
          if (!cv::solvePnP(m_phantomFiducialCoords, spheres, intrinsic, distCoeffs, rvec, tvec))
          {
            OutputDebugStringW(L"Unable to solve object pose.");
            result = false;
            goto done;
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
          result = true;
        }
done:
        delete buffer;
        delete reference;
      }

      return result;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputeTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, CameraRegistration::DetectedSpheresWorld& worldResults)
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

      float4x4 red5ToPhantomTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red5Sphere", L"Phantom"), &isValid);
      float4x4 red5ToReferenceTransform = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Red5Sphere", L"Reference"), &isValid);

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

      result = decompose(transpose(red5ToReferenceTransform), &scale, &quat, &translation);
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

        result = decompose(transpose(red5ToPhantomTransform), &scale, &quat, &translation);
        if (!result) { return result; }
        fiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        m_phantomFiducialCoords = fiducialCoords;
      }

      return true;
    }
  }
}