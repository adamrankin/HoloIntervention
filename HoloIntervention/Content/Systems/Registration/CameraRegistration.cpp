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
          std::vector<cv::Point3f> circles;

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
              radiusMean += circle.z;
            }
            radiusMean /= circles.size();

            for (auto& circle : circles)
            {
              // Ensure radius of circle falls within 15% of mean
              if (circle.z / radiusMean < 0.85f || circle.z / radiusMean > 1.15f)
              {
                result = false;
                goto done;
              }

              circleCentersPixel.push_back(cv::Point2f(circle.x, circle.y));
            }
          }
          else
          {
            // TODO : is it possible to make our code more robust by identifying 4 circles that make sense? pixel center distances? radii? etc...
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
          if (!SortCorrespondence(hsv, phantomFiducialsCv, circles))
          {
            result = false;
            goto done;
          }

          // Use iterative technique to refine results
          if (!cv::solvePnP(phantomFiducialsCv, circleCentersPixel, intrinsic, distCoeffs, rvec, tvec, false))
          {
            result = false;
            goto done;
          }

          std::stringstream ss;
          ss << "rvec: " << rvec << std::endl;
          ss << "tvec: " << tvec << std::endl;
          OutputDebugStringA(ss.str().c_str());

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
    bool CameraRegistration::SortCorrespondence(cv::Mat& image, std::vector<cv::Point3f>& inOutPhantomFiducialsCv, const std::vector<cv::Point3f>& inCircles)
    {
      const float SPHERE_RADIUS_MM = 15.f;
      const float MEAN_DISTRIBUTION_RATIO_THRESHOLD = 0.90f;
      const float FIFTH_PERCENTILE_FACTOR = 0.0627f;
      const float TANGENT_MM_COUNT = 5; // The number of mm to check on either side of the line connecting two circles

      byte* imageData = (byte*)image.data;

      // Build the list of patches to check
      typedef std::pair<uint32, uint32> Line; // indices into inCircles
      std::vector<Line> lines;
      lines.push_back(Line(0, 2));
      lines.push_back(Line(0, 3));
      lines.push_back(Line(1, 2));
      lines.push_back(Line(1, 3));
      lines.push_back(Line(2, 3));
      lines.push_back(Line(0, 1));

      enum Colours
      {
        green_link = 0,
        blue_link,
        yellow_link,
        colour_count
      };

      std::array<ColourToCircleList, colour_count> circleLinkResults;

      for (auto& line : lines)
      {
        // Given a pair of circles, compute histogram of patch that lies between, determine if profile of patch matches expected parameters
        auto firstCircle = inCircles[line.first];
        auto secondCircle = inCircles[line.second];
        float startMmToPixel = firstCircle.z / SPHERE_RADIUS_MM;
        float endMmToPixel = secondCircle.z / SPHERE_RADIUS_MM;

        float2 startCenter(firstCircle.x, firstCircle.y);
        float2 endCenter(secondCircle.x, secondCircle.y);
        float2 atVector(endCenter - startCenter);
        atVector = normalize(atVector);
        float2 tangentVector(atVector.y, atVector.x);
        float2 startPixel = startCenter + atVector * (SPHERE_RADIUS_MM + 0.25f) * startMmToPixel;
        float2 endPixel = endCenter - atVector * (SPHERE_RADIUS_MM + 0.25f) * endMmToPixel;

        HsvHistogram histogram;
        std::array<uint32, 3> hsvMeans;
        uint32 pixelCount(0);
        CalculatePatchHistogramHSV(startPixel, endPixel, atVector, tangentVector, TANGENT_MM_COUNT * (startMmToPixel < endMmToPixel ? startMmToPixel : endMmToPixel),
                                   imageData, image.step, histogram, hsvMeans, pixelCount);

        // Blue hue, 100-120, tuning needed
        uint8 blueHue[2] = { 100, 120 };
        if (IsPatchColour(blueHue, 70, 50, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[blue_link].push_back(line.first);
          circleLinkResults[blue_link].push_back(line.second);
        }

        // Green hue, 45-65, tuning needed
        uint8 greenHue[2] = { 50, 70 };
        if (IsPatchColour(greenHue, 70, 40, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[green_link].push_back(line.first);
          circleLinkResults[green_link].push_back(line.second);
        }

        // Yellow hue, 17-37, tuning needed
        uint8 yelloHue[2] = { 17, 37 };
        if (IsPatchColour(yelloHue, 70, 50, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[yellow_link].push_back(line.first);
          circleLinkResults[yellow_link].push_back(line.second);
        }
      }

      // If any pair of links has size 2, we can deduce the results
      auto& blueLinks = circleLinkResults[blue_link];
      auto& greenLinks = circleLinkResults[green_link];
      auto& yellowLinks = circleLinkResults[yellow_link];
      if (!((blueLinks.size() == 2 && greenLinks.size() == 2) ||
            (blueLinks.size() == 2 && yellowLinks.size() == 2) ||
            (greenLinks.size() == 2 && yellowLinks.size() == 2)))
      {
        // No pair of 2, cannot deduce pattern
        std::stringstream ss;
        ss << "Unable to deduce pattern. No 2 sets of detected links available. Blue: " << circleLinkResults[blue_link].size() << ", green: " << circleLinkResults[green_link].size() << ", yellow: " << circleLinkResults[yellow_link].size() << "." << std::endl;
        OutputDebugStringA(ss.str().c_str());
      }

      // Determine valid pair
      std::vector<uint32>* listA(nullptr);
      std::vector<uint32>* listB(nullptr);
      if (blueLinks.size() == 2 && yellowLinks.size() == 2)
      {
        listA = &blueLinks;
        listB = &yellowLinks;
      }
      else if (blueLinks.size() == 2 && greenLinks.size() == 2)
      {
        listA = &blueLinks;
        listB = &greenLinks;
      }
      else
      {
        listA = &greenLinks;
        listB = &yellowLinks;
      }

      // If this is a successful detection, there will be one and only one index common to all listA and listB
      int32 centerSphereIndex(-1);
      for (auto& circleIndex : *listA)
      {
        if (circleIndex != (*listB)[0] && circleIndex != (*listB)[1])
        {
          continue;
        }
        centerSphereIndex = circleIndex;
        break;
      }

      if (centerSphereIndex == -1)
      {
        OutputDebugStringA("No index common to all lists.\n");
        return false;
      }

      // Find it, remove it from the other lists, and the values remaining in those lists are the index of the other circles
      RemoveResultFromList(*listA, centerSphereIndex);
      RemoveResultFromList(*listB, centerSphereIndex);

      // Now we know one, and two others (based on which colour of list they're in)
      cv::Point3f greenCircle(0.f, 0.f, -1.f);
      cv::Point3f centerCircle = inOutPhantomFiducialsCv[centerSphereIndex];
      cv::Point3f yellowCircle(0.f, 0.f, -1.f);
      cv::Point3f blueCircle(0.f, 0.f, -1.f);
      std::vector<uint32> remainingColours = { 0, 2, 3 }; // 0 = green, 2 = yellow, 3 = blue
      std::vector<uint32> remainingIndicies = { 0, 1, 2, 3 };

      remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), centerSphereIndex));

      if (listA == &greenLinks || listB == &greenLinks)
      {
        greenCircle = inOutPhantomFiducialsCv[greenLinks[0]];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), greenLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 0));
      }
      if (listA == &blueLinks || listB == &blueLinks)
      {
        blueCircle = inOutPhantomFiducialsCv[blueLinks[0]];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), blueLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 3));
      }
      if (listA == &yellowLinks || listB == &yellowLinks)
      {
        yellowCircle = inOutPhantomFiducialsCv[yellowLinks[0]];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), yellowLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 2));
      }

      // One remaining circle has not yet been set, it's index (as per remaining colours above) remains
      assert(remainingColours.size() == 1);

      if (remainingColours[0] == 0)
      {
        greenCircle = inOutPhantomFiducialsCv[remainingIndicies[0]];
      }
      else if (remainingColours[0] == 2)
      {
        yellowCircle = inOutPhantomFiducialsCv[remainingIndicies[0]];
      }
      else
      {
        blueCircle = inOutPhantomFiducialsCv[remainingIndicies[0]];
      }

      std::vector<cv::Point3f> output;
      output.push_back(greenCircle);
      output.push_back(inCircles[centerSphereIndex]);
      output.push_back(yellowCircle);
      output.push_back(blueCircle);
      inOutPhantomFiducialsCv = output;

      std::stringstream ss;
      ss << "centers: " << output[0] << ", " << output[1] << ", " << output[2] << ", " << output[3] << std::endl;
      OutputDebugStringA(ss.str().c_str());

      return true;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::IsPatchColour(const uint8 hueRange[2], const uint8 saturationMin, const uint8 valueMin,
                                           const std::array<uint32, 3>& hsvMeans, const HsvHistogram& histogram,
                                           const uint32 pixelCount, const float percentileFactor, const float distributionRatio)
    {
      const uint32 HUE_MAX = 180;

      bool basicCheck = hsvMeans[0] >= hueRange[0] && hueRange[1] >= hsvMeans[0] && // does hue fall within requested range
                        hsvMeans[1] >= saturationMin &&                             // does saturation fall above or equal to requested minimum
                        hsvMeans[2] >= valueMin;                                    // does value fall above or equal to requested minimum

      if (!basicCheck)
      {
        return false;
      }

      // Now examine histogram around mean hue to see if the distribution is concentrated about the mean
      uint32 hueCountSum(0);
      uint32 hueRangeHalf = (uint32)(HUE_MAX * percentileFactor);
      for (uint32 i = hsvMeans[0] - hueRangeHalf; i < hsvMeans[0] + hueRangeHalf; ++i)
      {
        // ensure i is wrapped to 0-HUE_MAX
        hueCountSum += histogram.hue[(i + HUE_MAX) % HUE_MAX];
      }
      return (1.f * hueCountSum / pixelCount) > distributionRatio;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::CalculatePatchHistogramHSV(const Windows::Foundation::Numerics::float2& startPixel,
        const Windows::Foundation::Numerics::float2& endPixel,
        const Windows::Foundation::Numerics::float2& atVector,
        const Windows::Foundation::Numerics::float2& tangentVector, const float tangentPixelCount,
        byte* imageData, const cv::MatStep& step, HsvHistogram& outHSVHistogram,
        std::array<uint32, 3>& hsvMeans, uint32& outPixelCount)
    {
      outPixelCount = 0;
      auto atLength = length(endPixel - startPixel);
      float2 huePolarMean(0.f, 0.f);
      float meanSaturation(0.f);
      float meanValue(0.f);
      const float PI = 3.14159265359f;

      for (float i = 0.f; i < tangentPixelCount / 2; ++i)
      {
        for (float j = 0.f; j < atLength; ++j)
        {
          uint32 compositePixelValue = CalculatePixelValue(startPixel + tangentVector * i + atVector * j, imageData, step);
          uint8 hue = compositePixelValue >> 16;
          uint8 saturation = compositePixelValue >> 8;
          uint8 value = compositePixelValue;

          huePolarMean.x += cosf(hue / 90.f * PI);
          huePolarMean.y += sinf(hue / 90.f * PI);
          meanSaturation += saturation;
          meanValue += value;

          outHSVHistogram.hue[hue]++;
          outHSVHistogram.saturation[saturation]++;
          outHSVHistogram.value[value]++;
          outPixelCount++;
        }

        // Don't double count the center line
        if (i != 0.f)
        {
          for (float j = 0.f; j < atLength; ++j)
          {
            uint32 compositePixelValue = CalculatePixelValue(startPixel - tangentVector * i + atVector * j, imageData, step);
            uint8 hue = compositePixelValue >> 16;
            uint8 saturation = compositePixelValue >> 8;
            uint8 value = compositePixelValue;

            huePolarMean.x += cosf(hue / 90.f * PI);
            huePolarMean.y += sinf(hue / 90.f * PI);
            meanSaturation += saturation;
            meanValue += value;

            outHSVHistogram.hue[hue]++;
            outHSVHistogram.saturation[saturation]++;
            outHSVHistogram.value[value]++;
            outPixelCount++;
          }
        }
      }

      float hue = atan2f(huePolarMean.y / outPixelCount, huePolarMean.x / outPixelCount) * 90 / PI;
      if (hue < 0.f)
      {
        hue += 180.f;
      }
      hsvMeans[0] = (uint32)(hue);
      hsvMeans[1] = (uint32)(meanSaturation / outPixelCount);
      hsvMeans[2] = (uint32)(meanValue / outPixelCount);
    }

    //----------------------------------------------------------------------------
    uint32 CameraRegistration::CalculatePixelValue(const float2& currentPixelLocation, byte* imageData, const cv::MatStep& step)
    {
      //// addr(M_{i,j}) = M.data + M.step[0]*row + M.step[1]*col
      // cast = floor, cast + 1 = ceil, x/y guaranteed positive
      byte* lowerLeftPixel = &imageData[step[0] * (int)(currentPixelLocation.y + 1.f) + step[1] * (int)currentPixelLocation.x];
      byte* lowerRightPixel = &imageData[step[0] * (int)(currentPixelLocation.y + 1.f) + step[1] * (int)(currentPixelLocation.x + 1.f)];
      byte* upperLeftPixel = &imageData[step[0] * (int)currentPixelLocation.y + step[1] * (int)currentPixelLocation.x];
      byte* upperRightPixel = &imageData[step[0] * (int)currentPixelLocation.y + step[1] * (int)(currentPixelLocation.x + 1.f)];
      float ratio[2] = { fmodf(currentPixelLocation.x, 1.f), fmodf(currentPixelLocation.y, 1.f) };

      byte horizontalBlend[2][3] = { {
          (byte)((1.f - ratio[0])* lowerLeftPixel[0] + ratio[0] * lowerRightPixel[0]),
          (byte)((1.f - ratio[0])* lowerLeftPixel[1] + ratio[0] * lowerRightPixel[1]),
          (byte)((1.f - ratio[0])* lowerLeftPixel[2] + ratio[0] * lowerRightPixel[2])
        },
        {
          (byte)((1.f - ratio[0]) * upperLeftPixel[0] + ratio[0] * upperRightPixel[0]),
          (byte)((1.f - ratio[0]) * upperLeftPixel[1] + ratio[0] * upperRightPixel[1]),
          (byte)((1.f - ratio[0]) * upperLeftPixel[2] + ratio[0] * upperRightPixel[2])
        }
      };

      return (byte)(ratio[1] * horizontalBlend[0][0] + (1.f - ratio[1]) * horizontalBlend[1][0]) << 16 |
             (byte)(ratio[1] * horizontalBlend[0][1] + (1.f - ratio[1]) * horizontalBlend[1][1]) << 8 |
             (byte)(ratio[1] * horizontalBlend[0][2] + (1.f - ratio[1]) * horizontalBlend[1][2]);
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::RemoveResultFromList(ColourToCircleList& circleLinkResult, int32 centerSphereIndex)
    {
      if (circleLinkResult[0] == centerSphereIndex)
      {
        circleLinkResult.erase(circleLinkResult.begin());
      }
      else
      {
        circleLinkResult.pop_back();
      }
    }
  }
}