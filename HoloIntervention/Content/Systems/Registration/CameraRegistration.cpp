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
#include "Common.h"
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
      try
      {
        InitializeTransformRepositoryAsync(m_transformRepository, L"Assets\\Data\\configuration.xml").then([this]()
        {
          m_transformsAvailable = true;
        });
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(e->Message);
      }

      m_sphereCoordinateNames[0] = ref new UWPOpenIGTLink::TransformName(L"RedSphere1", L"Reference");
      m_sphereCoordinateNames[1] = ref new UWPOpenIGTLink::TransformName(L"RedSphere2", L"Reference");
      m_sphereCoordinateNames[2] = ref new UWPOpenIGTLink::TransformName(L"RedSphere3", L"Reference");
      m_sphereCoordinateNames[3] = ref new UWPOpenIGTLink::TransformName(L"RedSphere4", L"Reference");
      m_sphereCoordinateNames[4] = ref new UWPOpenIGTLink::TransformName(L"RedSphere5", L"Reference");
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
    Windows::Foundation::Numerics::float4x4 CameraRegistration::GetReferenceToHMD() const
    {
      return m_referenceToHMD;
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

      if (!m_transformsAvailable)
      {
        OutputDebugStringA("Unable to process frames. Transform repository was not properly initialized.");
        return;
      }

      while (!token.is_canceled())
      {
        if (m_videoFrameProcessor == nullptr || !HoloIntervention::instance()->GetIGTLink().IsConnected())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }

        UWPOpenIGTLink::TrackedFrame^ trackedFrame(nullptr);
        MediaFrameReference^ cameraFrame(m_videoFrameProcessor->GetLatestFrame());
        if (HoloIntervention::instance()->GetIGTLink().GetTrackedFrame(l_latestTrackedFrame, &m_latestTimestamp) &&
            cameraFrame != nullptr &&
            cameraFrame != l_latestCameraFrame)
        {
          m_latestTimestamp = l_latestTrackedFrame->Timestamp;
          l_latestCameraFrame = cameraFrame;
          if (m_worldCoordinateSystem != nullptr && l_latestCameraFrame->CoordinateSystem != nullptr)
          {
            try
            {
              Platform::IBox<float4x4>^ transformBox = l_latestCameraFrame->CoordinateSystem->TryGetTransformTo(m_worldCoordinateSystem);
              if (transformBox != nullptr)
              {
                m_cameraToHMD = transformBox->Value;
              }
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(L"Cannot retrieve camera to world transformation.\n");
              OutputDebugStringW(e->Message->Data());
              continue;
            }
          }

          DetectedSpheresWorld referenceResults;
          if (!RetrieveTrackerFrameLocations(l_latestTrackedFrame, referenceResults))
          {
            continue;
          }

          DetectedSpheresWorld cameraResults;
          if (!referenceResults.empty() && ComputeCircleLocations(l_latestCameraFrame->VideoMediaFrame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, cameraResults))
          {
            m_cameraFrameResults.push_back(cameraResults);
            m_referenceFrameResults.push_back(referenceResults);
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Acquired " + m_cameraFrameResults.size().ToString() + L" frame" + (m_cameraFrameResults.size() > 1 ? L"s" : L"") + L".",  0.5);
          }
        }
        else
        {
          // No new frame
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (m_cameraFrameResults.size() == NUMBER_OF_FRAMES_FOR_CALIBRATION)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Calculating registration...");
          DetectedSpheresWorld referenceResults;
          DetectedSpheresWorld cameraResults;
          for (auto& frame : m_referenceFrameResults)
          {
            for (auto& sphereWorld : frame)
            {
              referenceResults.push_back(sphereWorld);
            }
          }
          for (auto& frame : m_cameraFrameResults)
          {
            for (auto& sphereWorld : frame)
            {
              cameraResults.push_back(sphereWorld);
            }
          }
          m_landmarkRegistration->SetSourceLandmarks(referenceResults);
          m_landmarkRegistration->SetTargetLandmarks(cameraResults);

          std::atomic_bool calcFinished(false);
          bool resultValid(false);
          m_landmarkRegistration->CalculateTransformationAsync().then([this, &calcFinished, &resultValid](float4x4 referenceToCamera)
          {
            if (referenceToCamera == float4x4::identity())
            {
              resultValid = false;
            }
            else
            {
              resultValid = true;
              m_referenceToHMD = transpose(m_cameraToHMD) * referenceToCamera;
            }
            calcFinished = true;
          });

          while (!calcFinished)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }

          if (!resultValid)
          {
            m_referenceToHMD = float4x4::identity();
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration failed. Please repeat process.");
            StopCameraAsync();
          }
          else
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration complete.");
            StopCameraAsync();
          }

          m_cameraFrameResults.clear();
          m_referenceFrameResults.clear();
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
          cv::HoughCircles(mask, circles, CV_HOUGH_GRADIENT, 2, mask.rows / 16, 255, 30, 30, 60);

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
              // Ensure radius of circle falls within 15% of mean
              if (circle[2] / radiusMean < 0.85f || circle[2] / radiusMean > 1.15f)
              {
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

          cv::Mat rvec(3, 1, cv::DataType<float>::type);
          cv::Mat tvec(3, 1, rvec.type());
          if (!cv::solvePnP(m_phantomFiducialCoords, spheres, intrinsic, distCoeffs, rvec, tvec, false, cv::SOLVEPNP_EPNP))
          {
            result = false;
            goto done;
          }

          cv::Mat rotation(3, 3, rvec.type());
          cv::Rodrigues(rvec, rotation);

          cv::Mat modelToCameraTransform = cv::Mat::eye(4, 4, rvec.type());
          rotation.copyTo(modelToCameraTransform(cv::Rect(0, 0, 3, 3)));
          tvec.copyTo(modelToCameraTransform(cv::Rect(3, 0, 1, 3)));

          std::stringstream ss;
          ss << modelToCameraTransform;
          OutputDebugStringA(ss.str().c_str());

          std::vector<cv::Vec4f> cameraPointsHomogenous(m_phantomFiducialCoords.size());
          std::vector<cv::Vec4f> modelPointsHomogenous;
          for (auto& cameraPoint : m_phantomFiducialCoords)
          {
            modelPointsHomogenous.push_back(cv::Vec4f(cameraPoint.x, cameraPoint.y, cameraPoint.z, 1.f));
          }
          cv::transform(modelPointsHomogenous, cameraPointsHomogenous, modelToCameraTransform);

          cameraResults.clear();
          for (auto& cameraPoint : cameraPointsHomogenous)
          {
            cameraResults.push_back(DetectedSphereWorld(cameraPoint[0], cameraPoint[1], cameraPoint[2]));
          }

          result = true;
        }
done:
        delete buffer;
        delete reference;
      }

      return result;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::RetrieveTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, CameraRegistration::DetectedSpheresWorld& worldResults)
    {
      m_transformRepository->SetTransforms(trackedFrame);

      // Calculate world position from transforms in tracked frame
      bool isValid(false);
      float4x4 red1ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[0], &isValid));
      if (!isValid) {return false;}

      float4x4 red2ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[1], &isValid));
      float4x4 red3ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[2], &isValid));
      float4x4 red4ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[3], &isValid));
      float4x4 red5ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[4], &isValid));

      float4 origin = { 0.f, 0.f, 0.f, 1.f };
      float4 translation = transform(origin, red1ToReferenceTransform);
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      translation = transform(origin, red2ToReferenceTransform);
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      translation = transform(origin, red3ToReferenceTransform);
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      translation = transform(origin, red4ToReferenceTransform);
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      translation = transform(origin, red5ToReferenceTransform);
      worldResults.push_back(DetectedSphereWorld(translation.x, translation.y, translation.z));

      if (m_phantomFiducialCoords.empty())
      {
        // Phantom is rigid body, so only need to pull the values once
        float4x4 red1ToPhantomTransform = transpose(m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere1", L"Phantom"), &isValid));
        translation = transform(origin, red1ToPhantomTransform);
        m_phantomFiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        float4x4 red2ToPhantomTransform = transpose(m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere2", L"Phantom"), &isValid));
        translation = transform(origin, red2ToPhantomTransform);
        m_phantomFiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        float4x4 red3ToPhantomTransform = transpose(m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere3", L"Phantom"), &isValid));
        translation = transform(origin, red3ToPhantomTransform);
        m_phantomFiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        float4x4 red4ToPhantomTransform = transpose(m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere4", L"Phantom"), &isValid));
        translation = transform(origin, red4ToPhantomTransform);
        m_phantomFiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));

        float4x4 red5ToPhantomTransform = transpose(m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"RedSphere5", L"Phantom"), &isValid));
        translation = transform(origin, red5ToPhantomTransform);
        m_phantomFiducialCoords.push_back(cv::Point3f(translation.x, translation.y, translation.z));
      }

      return true;
    }
  }
}