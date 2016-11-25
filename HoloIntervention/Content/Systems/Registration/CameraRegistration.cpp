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
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Devices::Core;
using namespace Windows::Media::MediaProperties;
using namespace Windows::Perception::Spatial;

namespace
{
  //----------------------------------------------------------------------------
  bool ExtractPhantomToFiducialPose(float4x4& phantomFiducialPose, UWPOpenIGTLink::TransformRepository^ transformRepository, Platform::String^ from, Platform::String^ to)
  {
    bool isValid;
    float4 origin = { 0.f, 0.f, 0.f, 1.f };
    float4x4 transformation = float4x4::identity();
    try
    {
      phantomFiducialPose = transpose(transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(from, to), &isValid));
    }
    catch (Platform::Exception^ e)
    {
      return false;
    }

    return true;
  }
}

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
    }

    //----------------------------------------------------------------------------
    CameraRegistration::~CameraRegistration()
    {

    }

    //----------------------------------------------------------------------------
    void CameraRegistration::Update(Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToRequestedBox)
    {
      if (anchorToRequestedBox == nullptr)
      {
        return;
      }

      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != Rendering::INVALID_ENTRY && m_sphereInAnchorResults.size() > 0)
      {
        // Only do this if we've enabled visualization, the sphere primitives have been created, and we've analyzed at least 1 frame
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          auto entry = HoloIntervention::instance()->GetModelRenderer().GetPrimitive(m_spherePrimitiveIds[i]);
          float4x4 anchorToRequested = anchorToRequestedBox->Value;
          entry->SetDesiredWorldPose(m_sphereToAnchorPoses[i] * anchorToRequested);
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
      return create_task([]()
      {
        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> CameraRegistration::StartCameraAsync()
    {
      std::lock_guard<std::mutex> frameGuard(m_framesLock);
      m_sphereInAnchorResults.clear();
      m_sphereInTrackerResults.clear();

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
                  try
                  {
                    ProcessAvailableFrames(token);
                  }
                  catch (const std::exception& e)
                  {
                    HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Registration failed. Please retry.");
                    OutputDebugStringA(e.what());
                    m_tokenSource.cancel();
                    return;
                  }
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
          entry->SetDesiredWorldPose(float4x4::identity());
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
      cv::Mat l_rvec(3, 1, CV_32F);
      cv::Mat l_tvec(3, 1, CV_32F);
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

          VecFloat3 sphereInTrackerResults;
          std::array<float4x4, 4> sphereToPhantomPose;
          if (!RetrieveTrackerFrameLocations(l_latestTrackedFrame, sphereInTrackerResults, sphereToPhantomPose))
          {
            continue;
          }

          float4x4 phantomToCameraTransform;
          if (!sphereInTrackerResults.empty() && ComputePhantomToCameraTransform(l_latestCameraFrame->VideoMediaFrame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_rvec, l_tvec, l_canny_output, phantomToCameraTransform))
          {
            // Transform points in model space to anchor space
            VecFloat3 sphereInAnchorResults;
            int i = 0;
            for (auto& sphereToPhantom : m_sphereToPhantomPoses)
            {
              float4x4 sphereToAnchorPose = sphereToPhantom * phantomToCameraTransform * cameraToRawWorldAnchor;
              float3 sphereOriginInAnchorSpace = transform(float3(0.f, 0.f, 0.f), sphereToAnchorPose);
              sphereInAnchorResults.push_back(float3(sphereOriginInAnchorSpace.x, sphereOriginInAnchorSpace.y, sphereOriginInAnchorSpace.z));
              if (m_visualizationEnabled)
              {
                // If visualizing, update the latest known poses of the spheres
                m_sphereToAnchorPoses[i] = sphereToAnchorPose;
                i++;
              }
            }

            m_sphereInAnchorResults.push_back(sphereInAnchorResults);
            m_sphereInTrackerResults.push_back(sphereInTrackerResults);
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Acquired " + m_sphereInAnchorResults.size().ToString() + L"/" + NUMBER_OF_FRAMES_FOR_CALIBRATION.ToString() + " frames.",  0.5);
          }

          // If we've acquired enough frames, perform the registration
          if (m_sphereInAnchorResults.size() == NUMBER_OF_FRAMES_FOR_CALIBRATION)
          {
            PerformLandmarkRegistration();
          }
        }
        else
        {
          // No new frame
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::PerformLandmarkRegistration()
    {
      HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Calculating registration...");
      VecFloat3 sphereInTrackerResults;
      VecFloat3 sphereInAnchorResults;
      for (auto& frame : m_sphereInTrackerResults)
      {
        for (auto& sphereWorld : frame)
        {
          sphereInTrackerResults.push_back(sphereWorld);
        }
      }
      for (auto& frame : m_sphereInAnchorResults)
      {
        for (auto& sphereWorld : frame)
        {
          sphereInAnchorResults.push_back(sphereWorld);
        }
      }
      m_landmarkRegistration->SetSourceLandmarks(sphereInTrackerResults);
      m_landmarkRegistration->SetTargetLandmarks(sphereInAnchorResults);

      std::atomic_bool calcFinished(false);
      bool resultValid(false);
      m_landmarkRegistration->CalculateTransformationAsync().then([this, &calcFinished, &resultValid](float4x4 trackerToAnchorTransformation)
      {
        if (trackerToAnchorTransformation == float4x4::identity())
        {
          resultValid = false;
        }
        else
        {
          m_hasRegistration = true;
          resultValid = true;
          m_trackerToWorldAnchor = trackerToAnchorTransformation;
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

      m_sphereInAnchorResults.clear();
      m_sphereInTrackerResults.clear();
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::ComputePhantomToCameraTransform(VideoMediaFrame^ videoFrame,
        bool& initialized,
        int32_t& height,
        int32_t& width,
        cv::Mat& hsv,
        cv::Mat& redMat,
        cv::Mat& redMatWrap,
        cv::Mat& imageRGB,
        cv::Mat& mask,
        cv::Mat& rvec,
        cv::Mat& tvec,
        cv::Mat& cannyOutput,
        float4x4& phantomToCameraTransform)
    {
      if (m_sphereToPhantomPoses.size() != PHANTOM_SPHERE_COUNT)
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

          std::vector<cv::Point2f> circleCentersPixel;
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

              circleCentersPixel.push_back(cv::Point2f(circle[0], circle[1]));
            }
          }
          else
          {
            // TODO : is it possible to make our code more robust by identifying 5 circles that make sense? pixel center distances? radii? etc...
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

          std::vector<cv::Point3f> phantomFiducialsCv;
          float3 origin(0.f, 0.f, 0.f);
          for (auto& pose : m_sphereToPhantomPoses)
          {
            phantomFiducialsCv.push_back(cv::Point3f(pose.m41, pose.m42, pose.m43));
          }

          // Circles has x, y, radius
          SortCorrespondence(hsv, phantomFiducialsCv, circles);

          // Initialize iterative method with a EPnP approach
          if (m_sphereInAnchorResults.empty())
          {
            if (!cv::solvePnP(phantomFiducialsCv, circleCentersPixel, intrinsic, distCoeffs, rvec, tvec, false, cv::SOLVEPNP_EPNP))
            {
              result = false;
              goto done;
            }
          }

          // Now use iterative technique to refine results
          if (!cv::solvePnP(phantomFiducialsCv, circleCentersPixel, intrinsic, distCoeffs, rvec, tvec, true))
          {
            result = false;
            goto done;
          }

          cv::Mat rotation(3, 3, CV_32F);
          cv::Rodrigues(rvec, rotation);

          cv::Mat phantomToCameraTransformCv = cv::Mat::eye(4, 4, CV_32F);
          rotation.copyTo(phantomToCameraTransformCv(cv::Rect(0, 0, 3, 3)));
          tvec.copyTo(phantomToCameraTransformCv(cv::Rect(3, 0, 1, 3)));

          XMStoreFloat4x4(&phantomToCameraTransform, XMLoadFloat4x4(&XMFLOAT4X4((float*)phantomToCameraTransformCv.data)));

          // OpenCV -> +x right, +y down, +z away (RHS)
          // HoloLens -> +x right, +y up, +z towards (RHS)
          float4x4 cvToD3D = float4x4::identity();
          cvToD3D.m22 = -1.f;
          cvToD3D.m33 = -1.f;
          phantomToCameraTransform = transpose(phantomToCameraTransform) * cvToD3D; // Output is in column-major format, OpenCV produces row-major
          result = true;
        }
done:
        delete buffer;
        delete reference;
      }

      return result;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::RetrieveTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, CameraRegistration::VecFloat3& outSphereInReferenceResults, std::array<float4x4, 4>& outSphereToPhantomPose)
    {
      m_transformRepository->SetTransforms(trackedFrame);

      float4x4 red1ToReferenceTransform;
      float4x4 red2ToReferenceTransform;
      float4x4 red3ToReferenceTransform;
      float4x4 red4ToReferenceTransform;

      // Calculate world position from transforms in tracked frame
      try
      {
        bool isValid(false);
        float4x4 red1ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[0], &isValid));
        if (!isValid)
        {
          return false;
        }

        float4x4 red2ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[1], &isValid));
        float4x4 red3ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[2], &isValid));
        float4x4 red4ToReferenceTransform = transpose(m_transformRepository->GetTransform(m_sphereCoordinateNames[3], &isValid));
      }
      catch (Platform::Exception^ e)
      {
        return false;
      }

      float4 origin = { 0.f, 0.f, 0.f, 1.f };
      float4 translation = transform(origin, red1ToReferenceTransform);
      outSphereInReferenceResults.push_back(float3(translation.x, translation.y, translation.z));

      translation = transform(origin, red2ToReferenceTransform);
      outSphereInReferenceResults.push_back(float3(translation.x, translation.y, translation.z));

      translation = transform(origin, red3ToReferenceTransform);
      outSphereInReferenceResults.push_back(float3(translation.x, translation.y, translation.z));

      translation = transform(origin, red4ToReferenceTransform);
      outSphereInReferenceResults.push_back(float3(translation.x, translation.y, translation.z));

      // Phantom is rigid body, so only need to pull the values once
      if (!m_hasSpherePoses)
      {
        bool hasError(false);
        if (!ExtractPhantomToFiducialPose(m_sphereToPhantomPoses[0], m_transformRepository, L"RedSphere1", L"Phantom"))
        {
          hasError = true;
        }

        if (!ExtractPhantomToFiducialPose(m_sphereToPhantomPoses[1], m_transformRepository, L"RedSphere2", L"Phantom"))
        {
          hasError = true;
        }

        if (!ExtractPhantomToFiducialPose(m_sphereToPhantomPoses[2], m_transformRepository, L"RedSphere3", L"Phantom"))
        {
          hasError = true;
        }

        if (!ExtractPhantomToFiducialPose(m_sphereToPhantomPoses[3], m_transformRepository, L"RedSphere4", L"Phantom"))
        {
          hasError = true;
        }

        if (!hasError)
        {
          m_hasSpherePoses = true;
        }
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
        for (auto& frame : m_sphereInAnchorResults)
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
      for (auto& pose : m_sphereToAnchorPoses)
      {
        pose = pose * args->OldRawCoordinateSystemToNewRawCoordinateSystemTransform;
      }
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::SortCorrespondence(cv::Mat& image, std::vector<cv::Point3f>& inOutPhantomFiducialsCv, const std::vector<cv::Vec3f>& inCircles)
    {
      const float TWO_PI = 6.28318530718f;
      const uint32 NUMBER_OF_PATCHES = 24;
      const float TANGENT_MM_COUNT = 3.75f; // Check patches of 4mm x 10mm
      const float RADIAL_MM_COUNT = 10.f;
      const float SPHERE_RADIUS_MM = 15.f;
      const float MEAN_DISTRIBUTION_RATIO_THRESHOLD = 0.95f;
      const uint8 FIVE_PCT_PIXEL_RANGE = 16;

      byte* imageData = (byte*)image.data;

      std::array<std::array<std::array<uint32, 255>, 3>, PHANTOM_SPHERE_COUNT> hsvHistograms;

      uint32 circleIndex(0);
      for (auto& circle : inCircles)
      {
        uint32 blueCount(0);
        uint32 whiteCount(0);
        uint32 greenCount(0);

        // given a circle, calculate radial patches, compute histogram, determine if profile of any patches match expected parameters
        float mmToPixel = SPHERE_RADIUS_MM / circle[2];
        for (uint32 currentPatchIndex = 0; currentPatchIndex < NUMBER_OF_PATCHES; ++currentPatchIndex)
        {
          float2 radialVector(cos(TWO_PI * currentPatchIndex / NUMBER_OF_PATCHES), sin(TWO_PI * currentPatchIndex / NUMBER_OF_PATCHES));
          float2 tangentVector(radialVector.y, radialVector.x);
          float2 radialOriginPixel = radialVector * (SPHERE_RADIUS_MM + 0.25f) * mmToPixel; // Grab a point just outside the circle in image space

          CalculatePatchHistogramHSV(TANGENT_MM_COUNT, RADIAL_MM_COUNT, mmToPixel, radialOriginPixel, tangentVector, radialVector,
                                     imageData, image.step, hsvHistograms[circleIndex]);

          uint32 totalPixelCount(TANGENT_MM_COUNT * mmToPixel * RADIAL_MM_COUNT * mmToPixel);

          // Blue hue, 100-120 ish, tuning needed
          // Green hue, 45-65 ish, tuning needed

          // Determine if patch is white
          // low sat, high value
          uint32 lowSatPixelCount(0);
          for (int i = 0; i < FIVE_PCT_PIXEL_RANGE; ++i)
          {
            lowSatPixelCount += hsvHistograms[circleIndex][1][i];
          }
          float lowSatRatio = (1.f * lowSatPixelCount) / totalPixelCount;
          if (lowSatRatio > 0.9f)
          {
            uint32 highValPixelCount(0);
            for (int i = 0; i < FIVE_PCT_PIXEL_RANGE; ++i)
            {
              highValPixelCount += hsvHistograms[circleIndex][2][255 - i];
            }
            float highValRatio = (1.f * highValPixelCount) / totalPixelCount;
            if (highValRatio > 0.9f)
            {
              whiteCount++;
            }
          }

          // Determine if patch is blue
          //cv::inRange(hsv, cv::Scalar(100, 70, 50), cv::Scalar(120, 255, 255), redMat);
        }

        // Patches have been scanned, let's see if this sphere's configuration matches any expected pattern
        circleIndex++;
      }
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::CalculatePatchHistogramHSV(const float TANGENT_MM_COUNT, const float RADIAL_MM_COUNT, float mmToPixel,
        const float2& radialOriginPixel, const float2& tangentVector, const float2& radialVector, byte* imageData, const cv::MatStep& step,
        std::array<std::array<uint32, 255>, 3>& hsvHistogram)
    {
      std::array<uint8, 3> patchMeanColour = { 0, 0, 0 };
      uint32 pixelCount(0);

      for (float i = 0.f; i < TANGENT_MM_COUNT / 2 * mmToPixel; ++i)
      {
        for (float j = 0.f; j < RADIAL_MM_COUNT * mmToPixel; ++j)
        {
          // addr(M_{i,j}) = M.data + M.step[0]*i + M.step[1]*j
          float2 currentPixelLocation = radialOriginPixel + tangentVector * i + radialVector * j;
          uint32 finalPixelValue = CalculatePixelValue(currentPixelLocation, imageData, step);
          uint8 hue = finalPixelValue >> 16;
          uint8 saturation = finalPixelValue >> 8;
          uint8 value = finalPixelValue;

          hsvHistogram[0][hue]++;
          hsvHistogram[1][saturation]++;
          hsvHistogram[2][value]++;
          patchMeanColour[0] += hue;
          patchMeanColour[1] += saturation;
          patchMeanColour[2] += value;
          pixelCount++;
        }
        for (float j = 0.f; j < RADIAL_MM_COUNT * mmToPixel; ++j)
        {
          float2 currentPixelLocation = radialOriginPixel - tangentVector * i + radialVector * j;
          uint32 finalPixelValue = CalculatePixelValue(currentPixelLocation, imageData, step);
          uint8 hue = finalPixelValue >> 16;
          uint8 saturation = finalPixelValue >> 8;
          uint8 value = finalPixelValue;

          hsvHistogram[0][hue]++;
          hsvHistogram[1][saturation]++;
          hsvHistogram[2][value]++;
          patchMeanColour[0] += hue;
          patchMeanColour[1] += saturation;
          patchMeanColour[2] += value;
          pixelCount++;
        }
      }

      patchMeanColour[0] = patchMeanColour[0] / pixelCount;
      patchMeanColour[1] = patchMeanColour[1] / pixelCount;
      patchMeanColour[2] = patchMeanColour[2] / pixelCount;
      return patchMeanColour;
    }

    //----------------------------------------------------------------------------
    uint32 CameraRegistration::CalculatePixelValue(const float2& currentPixelLocation, byte* imageData, const cv::MatStep& step)
    {
      // cast = floor, cast + 1 = ceil, x/y guaranteed positive
      byte* lowerLeftPixel = &imageData[step[0] * (int)currentPixelLocation.x + step[1] * (int)currentPixelLocation.y];
      byte* lowerRightPixel = &imageData[step[0] * (int)(currentPixelLocation.x + 1.f) + step[1] * (int)currentPixelLocation.y];
      byte* upperLeftPixel = &imageData[step[0] * (int)currentPixelLocation.x + step[1] * (int)(currentPixelLocation.y + 1.f)];
      byte* upperRightPixel = &imageData[step[0] * (int)(currentPixelLocation.x + 1.f) + step[1] * (int)(currentPixelLocation.y + 1.f)];
      float ratio[2] = { fmodf(currentPixelLocation.x, 1.f), fmodf(currentPixelLocation.y, 1.f) };

      byte horizontalBlend[2][3] = { {
          ratio[0]* lowerLeftPixel[0] + (1.f - ratio[0])* lowerRightPixel[0],
          ratio[0]* lowerLeftPixel[1] + (1.f - ratio[0])* lowerRightPixel[1],
          ratio[0]* lowerLeftPixel[2] + (1.f - ratio[0])* lowerRightPixel[2]
        },
        {
          ratio[0]* upperLeftPixel[0] + (1.f - ratio[0])* upperRightPixel[0],
          ratio[0]* upperLeftPixel[1] + (1.f - ratio[0])* upperRightPixel[1],
          ratio[0]* upperLeftPixel[2] + (1.f - ratio[0])* upperRightPixel[2]
        }
      };

      return (byte)(ratio[1] * horizontalBlend[0][0] + (1.f - ratio[1]) * horizontalBlend[1][0]) << 16 | (byte)(ratio[1] * horizontalBlend[0][1] + (1.f - ratio[1]) * horizontalBlend[1][1]) << 8 |
             (byte)(ratio[1] * horizontalBlend[0][2] + (1.f - ratio[1]) * horizontalBlend[1][2]);
    }
  }
}