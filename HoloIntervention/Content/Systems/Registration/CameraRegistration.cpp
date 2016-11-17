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
#include "InstancedGeometricPrimitive.h"
#include "VideoFrameProcessor.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"

// System includes
#include "NotificationSystem.h"
#include "ModelRenderer.h"

// WinRT includes
#include <ppl.h>

// STL includes
#include <algorithm>

// Network includes
#include "IGTLinkIF.h"

// OpenCV includes
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>

// Unnecessary, but eliminates intellisense errors
#include <WindowsNumerics.h>

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
    const float CameraRegistration::VISUALIZATION_SPHERE_RADIUS = 0.03f;

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
    void CameraRegistration::Update(Platform::IBox<Windows::Foundation::Numerics::float4x4>^ worldAnchorToRequestedBox)
    {
      if (worldAnchorToRequestedBox == nullptr)
      {
        return;
      }

      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != Rendering::INVALID_ENTRY)
      {
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          auto entry = HoloIntervention::instance()->GetModelRenderer().GetPrimitive(m_spherePrimitiveIds[i]);
          float4x4 anchorToRequested = worldAnchorToRequestedBox->Value;
          entry->SetDesiredWorldPose(m_sphereToAnchor[i] * anchorToRequested);
        }
      }
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::HasRegistration() const
    {
      return m_hasRegistration;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 CameraRegistration::GetTrackerToWorldAnchorTransformation() const
    {
      return m_trackerToWorldAnchor;
    }

    //----------------------------------------------------------------------------
    task<bool> CameraRegistration::StopCameraAsync()
    {
      if (m_videoFrameProcessor != nullptr && m_videoFrameProcessor->IsStarted())
      {
        m_tokenSource.cancel();
        std::lock_guard<std::mutex> processorGuard(m_processorLock);
        return m_videoFrameProcessor->StopAsync().then([this]() -> bool
        {
          m_videoFrameProcessor = nullptr;
          m_createTask = nullptr;
          m_phantomFiducialCoords.clear();
          m_transformsAvailable = false;
          m_latestTimestamp = 0.0;
          m_tokenSource = cancellation_token_source();
          m_currentFrame = nullptr;
          m_nextFrame = nullptr;
          m_workerTask = nullptr;
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Capturing stopped.");
          return true;
        });
      }
      return create_task([]() {return true;});
    }

    //----------------------------------------------------------------------------
    task<bool> CameraRegistration::StartCameraAsync()
    {
      std::lock_guard<std::mutex> frameGuard(m_framesLock);
      m_rawWorldAnchorResults.clear();
      m_trackerFrameResults.clear();

      return StopCameraAsync().then([this](bool result)
      {
        std::lock_guard<std::mutex> guard(m_processorLock);
        if (m_videoFrameProcessor == nullptr)
        {
          m_createTask = &Capture::VideoFrameProcessor::CreateAsync();
          return m_createTask->then([this](std::shared_ptr<Capture::VideoFrameProcessor> processor)
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
                return true;
              }
              return false;
            });
          });
        }
        else
        {
          return m_videoFrameProcessor->StartAsync().then([this](MediaFrameReaderStartStatus status) -> bool
          {
            return status == MediaFrameReaderStartStatus::Success;
          });
        }
      });
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::SetVisualization(bool enabled)
    {
      if (!enabled)
      {
        for (auto& sphereId : m_spherePrimitiveIds)
        {
          if (sphereId != Rendering::INVALID_ENTRY)
          {
            auto entry = HoloIntervention::instance()->GetModelRenderer().GetPrimitive(sphereId);
            entry->SetVisible(false);
          }
        }
        m_visualizationEnabled = false;
        return;
      }

      if (enabled && m_spherePrimitiveIds[0] == Rendering::INVALID_ENTRY)
      {
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          m_spherePrimitiveIds[i] = HoloIntervention::instance()->GetModelRenderer().AddGeometricPrimitive(
                                      std::move(DirectX::InstancedGeometricPrimitive::CreateSphere(m_deviceResources->GetD3DDeviceContext(), VISUALIZATION_SPHERE_RADIUS, 30))
                                    );
          auto entry = HoloIntervention::instance()->GetModelRenderer().GetPrimitive(m_spherePrimitiveIds[i]);
          entry->SetVisible(true);
          entry->SetColour(float3(0.803921640f, 0.360784322f, 0.360784322f));
        }
        m_visualizationEnabled = true;
      }
    }

    //----------------------------------------------------------------------------
    Windows::Perception::Spatial::SpatialAnchor^ CameraRegistration::GetWorldAnchor()
    {
      std::lock_guard<std::mutex> guard(m_anchorMutex);
      return m_worldAnchor;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor)
    {
      std::lock_guard<std::mutex> guard(m_anchorMutex);
      if (m_workerTask != nullptr)
      {
        // World anchor changed during registration, invalidate the registration session.
        StopCameraAsync().then([this](bool result)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"World anchor changed during registration. Aborting... please restart.");
          return;
        });
      }

      if (m_hasRegistration && m_worldAnchor != nullptr)
      {
        try
        {
          auto worldAnchorToNewAnchorBox = m_worldAnchor->CoordinateSystem->TryGetTransformTo(worldAnchor->CoordinateSystem);
          // If possible, update the registration to be referential to the new world anchor
          m_trackerToWorldAnchor = m_trackerToWorldAnchor * worldAnchorToNewAnchorBox->Value;
        }
        catch (Platform::Exception^ e) {}
      }

      if (m_worldAnchor != worldAnchor && m_worldAnchor != nullptr)
      {
        m_worldAnchor->RawCoordinateSystemAdjusted -= m_anchorUpdatedToken;
      }

      m_worldAnchor = worldAnchor;
      // Register for RawCoordinateSystemAdjusted event so that we can update the registration as needed
      m_anchorUpdatedToken = m_worldAnchor->RawCoordinateSystemAdjusted +=
                               ref new Windows::Foundation::TypedEventHandler<SpatialAnchor^, SpatialAnchorRawCoordinateSystemAdjustedEventArgs^>(
                                 std::bind(&CameraRegistration::OnAnchorRawCoordinateSystemAdjusted, this, std::placeholders::_1, std::placeholders::_2));
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

        std::lock_guard<std::mutex> frameGuard(m_framesLock);
        UWPOpenIGTLink::TrackedFrame^ trackedFrame(nullptr);
        MediaFrameReference^ cameraFrame(m_videoFrameProcessor->GetLatestFrame());
        if (HoloIntervention::instance()->GetIGTLink().GetTrackedFrame(l_latestTrackedFrame, &m_latestTimestamp) &&
            cameraFrame != nullptr &&
            cameraFrame != l_latestCameraFrame)
        {
          float4x4 cameraToRawWorldAnchor = float4x4::identity();

          m_latestTimestamp = l_latestTrackedFrame->Timestamp;
          l_latestCameraFrame = cameraFrame;
          if (l_latestCameraFrame->CoordinateSystem != nullptr)
          {
            std::lock_guard<std::mutex> guard(m_anchorMutex);
            Platform::IBox<float4x4>^ cameraToRawWorldAnchorBox;
            try
            {
              cameraToRawWorldAnchorBox = l_latestCameraFrame->CoordinateSystem->TryGetTransformTo(m_worldAnchor->RawCoordinateSystem);
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW((L"Exception: " + e->Message)->Data());
              continue;
            }
            if (cameraToRawWorldAnchorBox != nullptr)
            {
              cameraToRawWorldAnchor = cameraToRawWorldAnchorBox->Value;
            }
          }

          if (cameraToRawWorldAnchor == float4x4::identity())
          {
            continue;
          }

          DetectedSpheresWorld trackerResults;
          if (!RetrieveTrackerFrameLocations(l_latestTrackedFrame, trackerResults))
          {
            continue;
          }

          DetectedSpheresWorld cameraResults;
          if (!trackerResults.empty() && ComputeCircleLocations(l_latestCameraFrame->VideoMediaFrame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_canny_output, cameraResults))
          {
            // Results are in camera view coordinate system, transform to world anchor coordinate system
            std::vector<cv::Point3f> worldAnchorResults;
            for (auto& cameraPoint : cameraResults)
            {
              float3 cameraPointNumerics(cameraPoint.x, cameraPoint.y, cameraPoint.z);
              float3 point = transform(cameraPointNumerics, cameraToRawWorldAnchor);
              worldAnchorResults.push_back(cv::Point3f(point.x, point.y, point.z));
            }

            // If visualizing, update the latest known poses of the spheres
            if (m_visualizationEnabled && worldAnchorResults.size() == PHANTOM_SPHERE_COUNT)
            {
              for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
              {
                m_sphereToAnchorPoses[i] = make_float4x4_translation(float3(worldAnchorResults[i].x, worldAnchorResults[i].y, worldAnchorResults[i].z));
              }
            }

            m_rawWorldAnchorResults.push_back(worldAnchorResults);
            m_trackerFrameResults.push_back(trackerResults);
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Acquired " + m_rawWorldAnchorResults.size().ToString() + L" frame" + (m_rawWorldAnchorResults.size() > 1 ? L"s" : L"") + L".",  0.5);
          }
        }
        else
        {
          // No new frame
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (m_rawWorldAnchorResults.size() == NUMBER_OF_FRAMES_FOR_CALIBRATION)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Calculating registration...");
          DetectedSpheresWorld trackerResults;
          DetectedSpheresWorld worldAnchorResults;
          for (auto& frame : m_trackerFrameResults)
          {
            for (auto& sphereWorld : frame)
            {
              trackerResults.push_back(sphereWorld);
            }
          }
          for (auto& frame : m_rawWorldAnchorResults)
          {
            for (auto& sphereWorld : frame)
            {
              worldAnchorResults.push_back(sphereWorld);
            }
          }
          m_landmarkRegistration->SetSourceLandmarks(trackerResults);
          m_landmarkRegistration->SetTargetLandmarks(worldAnchorResults);

          std::atomic_bool calcFinished(false);
          bool resultValid(false);
          m_landmarkRegistration->CalculateTransformationAsync().then([this, &calcFinished, &resultValid](float4x4 trackerToWorldAnchor)
          {
            if (trackerToWorldAnchor == float4x4::identity())
            {
              resultValid = false;
            }
            else
            {
              m_hasRegistration = true;
              resultValid = true;
              m_trackerToWorldAnchor = trackerToWorldAnchor;
            }
            calcFinished = true;
          });

          while (!calcFinished)
          {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }

          if (!resultValid)
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration failed. Please repeat process.");
            StopCameraAsync();
          }
          else
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration complete.");
            StopCameraAsync();
          }

          m_rawWorldAnchorResults.clear();
          m_trackerFrameResults.clear();
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
        return false;
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

          std::vector<cv::Vec4f> cameraPointsHomogenous(m_phantomFiducialCoords.size());
          std::vector<cv::Vec4f> modelPointsHomogenous;
          for (auto& modelPoint : m_phantomFiducialCoords)
          {
            modelPointsHomogenous.push_back(cv::Vec4f(modelPoint.x, modelPoint.y, modelPoint.z, 1.f));
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

    //----------------------------------------------------------------------------
    void CameraRegistration::OnAnchorRawCoordinateSystemAdjusted(Windows::Perception::Spatial::SpatialAnchor^ anchor, Windows::Perception::Spatial::SpatialAnchorRawCoordinateSystemAdjustedEventArgs^ args)
    {
      if (m_hasRegistration)
      {
        m_trackerToWorldAnchor = m_trackerToWorldAnchor * args->OldRawCoordinateSystemToNewRawCoordinateSystemTransform;
      }

      if (m_workerTask != nullptr)
      {
        // Registration is active, apply this to all m_rawWorldAnchorResults results, to adjust raw anchor coordinate system that they depend on
        std::lock_guard<std::mutex> frameGuard(m_framesLock);
        for (auto& frame : m_rawWorldAnchorResults)
        {
          for (auto& point : frame)
          {
            float3 pointNumerics(point.x, point.y, point.z);
            pointNumerics = transform(pointNumerics, args->OldRawCoordinateSystemToNewRawCoordinateSystemTransform);
            point.x = pointNumerics.x;
            point.y = pointNumerics.y;
            point.z = pointNumerics.z;
          }
        }
      }
      for (auto& pose : m_sphereToAnchor)
      {
        pose = pose * args->OldRawCoordinateSystemToNewRawCoordinateSystemTransform;
      }
    }
  }
}