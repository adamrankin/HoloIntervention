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
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "ModelRenderer.h"

// WinRT includes
#include <ppl.h>

// STL includes
#include <algorithm>

// Network includes
#include "IGTConnector.h"

// OpenCV includes
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// Unnecessary, but eliminates intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

#ifndef SUCCEEDED
  #define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Capture::Frames;
using namespace Windows::Media::Devices::Core;
using namespace Windows::Media::MediaProperties;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace
{
  //----------------------------------------------------------------------------
  bool ExtractPhantomToFiducialPose(float4x4& phantomFiducialPose, UWPOpenIGTLink::TransformRepository^ transformRepository, Platform::String^ from, Platform::String^ to)
  {
    float4x4 transformation;
    if (transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(from, to), &transformation))
    {
      phantomFiducialPose = transpose(transformation);
      return true;
    }

    return false;
  }
}

namespace HoloIntervention
{
  namespace System
  {
    const float CameraRegistration::VISUALIZATION_SPHERE_RADIUS = 0.03f;

    //----------------------------------------------------------------------------
    float3 CameraRegistration::GetStabilizedPosition() const
    {
      float3 position(0.f, 0.f, 0.f);
      // Only do this if we've enabled visualization, the sphere primitives have been created, and we've analyzed at least 1 frame
      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != INVALID_TOKEN && m_sphereInAnchorResultFrames.size() > 0)
      {
        // Take the mean position
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          position += transform(float3(0.f, 0.f, 0.f), m_spherePrimitives[i]->GetCurrentPose());
        }
        position /= static_cast<float>(PHANTOM_SPHERE_COUNT);
      }

      return position;
    }

    //----------------------------------------------------------------------------
    float3 CameraRegistration::GetStabilizedNormal(SpatialPointerPose^ pose) const
    {
      return -pose->Head->ForwardDirection;
    }

    //----------------------------------------------------------------------------
    float3 CameraRegistration::GetStabilizedVelocity() const
    {
      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != INVALID_TOKEN && m_sphereInAnchorResultFrames.size() > 0)
      {
        // They all have the same velocity
        return m_spherePrimitives[0]->GetVelocity();
      }

      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float CameraRegistration::GetStabilizePriority() const
    {
      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != INVALID_TOKEN && m_sphereInAnchorResultFrames.size() > 0)
      {
        bool anyInFrustum(false);
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          anyInFrustum |= m_spherePrimitives[i]->IsInFrustum();
        }

        // TODO : priority values?
        return anyInFrustum ? 3.f : PRIORITY_NOT_ACTIVE;
      }

      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    CameraRegistration::CameraRegistration(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer, StorageFolder^ configStorageFolder)
      : m_modelRenderer(modelRenderer)
      , m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_configStorageFolder(configStorageFolder)
    {
      Init();
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

      if (m_visualizationEnabled && m_spherePrimitiveIds[0] != INVALID_TOKEN && m_sphereInAnchorResultFrames.size() > 0)
      {
        // Only do this if we've enabled visualization, the sphere primitives have been created, and we've analyzed at least 1 frame
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          m_spherePrimitives[i]->SetDesiredPose(m_sphereToAnchorPoses[i] * anchorToRequestedBox->Value);
        }
      }
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::HasRegistration() const
    {
      return m_hasRegistration;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 CameraRegistration::GetReferenceToWorldAnchorTransformation() const
    {
      return m_referenceToAnchor;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::RegisterTransformUpdatedCallback(std::function<void(float4x4)> function)
    {
      m_completeCallback = function;
    }

    //----------------------------------------------------------------------------
    task<bool> CameraRegistration::StopCameraAsync()
    {
      SetVisualization(false);

      if (IsCameraActive())
      {
        m_tokenSource.cancel();
        std::lock_guard<std::mutex> guard(m_processorLock);
        return m_videoFrameProcessor->StopAsync().then([this](task<void> stopTask)
        {
          try
          {
            stopTask.wait();
          }
          catch (Platform::Exception^ e)
          {
            OutputDebugStringW(e->Message->Data());
          }

          m_cameraActive = false;
          {
            std::lock_guard<std::mutex> processorGuard(m_processorLock);
            m_videoFrameProcessor = nullptr;
          }
          m_transformsAvailable = false;
          m_latestTimestamp = 0.0;
          m_tokenSource = cancellation_token_source();
          m_currentFrame = nullptr;
          m_pnpNeedsInit = true;
          m_nextFrame = nullptr;
          m_notificationSystem.QueueMessage(L"Capturing stopped.");
          Init();
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
      std::lock_guard<std::mutex> frameGuard(m_outputFramesLock);
      m_sphereInAnchorResultFrames.clear();
      m_sphereInReferenceResultFrames.clear();

      return StopCameraAsync().then([this](bool result)
      {
        SetVisualization(true);

        std::lock_guard<std::mutex> guard(m_processorLock);
        if (m_videoFrameProcessor == nullptr)
        {
          return Capture::VideoFrameProcessor::CreateAsync().then([this](task<std::shared_ptr<Capture::VideoFrameProcessor>> createTask)
          {
            std::shared_ptr<Capture::VideoFrameProcessor> processor = nullptr;
            try
            {
              processor = createTask.get();
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(e->Message->Data());
              return false;
            }

            if (processor == nullptr)
            {
              m_notificationSystem.QueueMessage(L"Unable to initialize capture system.");
            }
            else
            {
              std::lock_guard<std::mutex> guard(m_processorLock);
              m_videoFrameProcessor = processor;
            }

            return true;
          }).then([this](bool result)
          {
            if (!result)
            {
              return task_from_result(result);
            }

            std::lock_guard<std::mutex> guard(m_processorLock);
            return m_videoFrameProcessor->StartAsync().then([this](task<MediaFrameReaderStartStatus> startTask)
            {
              MediaFrameReaderStartStatus status;
              try
              {
                status = startTask.get();
              }
              catch (Platform::Exception^ e)
              {
                OutputDebugStringW(e->Message->Data());
                return false;
              }

              if (status == MediaFrameReaderStartStatus::Success)
              {
                m_notificationSystem.QueueMessage(L"Capturing...");
              }
              else
              {
                m_notificationSystem.QueueMessage(L"Unable to start capturing.");
              }

              m_cameraActive = true;
              return true;
            }).then([this](bool result)
            {
              if (!result)
              {
                return result;
              }

              m_tokenSource = cancellation_token_source();
              create_task([this]()
              {
                try
                {
                  ProcessAvailableFrames(m_tokenSource.get_token());
                }
                catch (const std::exception& e)
                {
                  Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e.what());
                  m_tokenSource.cancel();
                  return;
                }
              });
              create_task([this]()
              {
                try
                {
                  PerformLandmarkRegistration(m_tokenSource.get_token());
                }
                catch (const std::exception& e)
                {
                  Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e.what());
                  m_tokenSource.cancel();
                  return;
                }
              });
              return true;
            });
          });
        }
        else
        {
          std::lock_guard<std::mutex> processorGuard(m_processorLock);
          return m_videoFrameProcessor->StartAsync().then([this](task<MediaFrameReaderStartStatus> startTask)
          {
            MediaFrameReaderStartStatus status;
            try
            {
              status = startTask.get();
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(e->Message->Data());
              return false;
            }

            return status == MediaFrameReaderStartStatus::Success;
          });
        }
      });
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::IsCameraActive() const
    {
      return m_cameraActive;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::SetVisualization(bool on)
    {
      if (!on)
      {
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          if (m_spherePrimitiveIds[i] != INVALID_TOKEN)
          {
            m_spherePrimitives[i]->SetVisible(false);
          }
        }
        m_visualizationEnabled = false;
        return;
      }

      for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
      {
        if (m_spherePrimitiveIds[i] == INVALID_TOKEN)
        {
          m_spherePrimitiveIds[i] = m_modelRenderer.AddPrimitive(Rendering::PrimitiveType_SPHERE, VISUALIZATION_SPHERE_RADIUS, 30);
          m_spherePrimitives[i] = m_modelRenderer.GetPrimitive(m_spherePrimitiveIds[i]);
          m_spherePrimitives[i]->SetVisible(true);
          m_spherePrimitives[i]->SetColour(float3(0.803921640f, 0.360784322f, 0.360784322f));
          m_spherePrimitives[i]->SetDesiredPose(float4x4::identity());
        }
        else
        {
          m_spherePrimitives[i]->SetVisible(true);
        }
      }
      m_visualizationEnabled = true;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::DiscardFrames()
    {
      std::lock_guard<std::mutex> guard(m_outputFramesLock);
      m_lastRegistrationResultCount = NUMBER_OF_FRAMES_BETWEEN_REGISTRATION;
      m_referenceToAnchor = float4x4::identity();
      m_pnpNeedsInit = true;
      m_sphereInAnchorResultFrames.clear();
      m_sphereInReferenceResultFrames.clear();
    }

    //----------------------------------------------------------------------------
    Windows::Perception::Spatial::SpatialAnchor^ CameraRegistration::GetWorldAnchor()
    {
      std::lock_guard<std::mutex> guard(m_anchorLock);
      return m_worldAnchor;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor)
    {
      std::lock_guard<std::mutex> guard(m_anchorLock);
      if (IsCameraActive())
      {
        // World anchor changed during registration, invalidate the registration session.
        StopCameraAsync().then([this](bool result)
        {
          m_notificationSystem.QueueMessage(L"World anchor changed during registration. Aborting... please restart.");
          return;
        });
      }

      if (m_hasRegistration && m_worldAnchor != nullptr)
      {
        try
        {
          Platform::IBox<float4x4>^ worldAnchorToNewAnchorBox(nullptr);
          try
          {
            worldAnchorToNewAnchorBox = m_worldAnchor->CoordinateSystem->TryGetTransformTo(worldAnchor->CoordinateSystem);
          }
          catch (Platform::Exception^ e)
          {
            Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
          }
          if (worldAnchorToNewAnchorBox != nullptr)
          {
            // If possible, update the registration to be referential to the new world anchor
            m_referenceToAnchor = m_referenceToAnchor * worldAnchorToNewAnchorBox->Value;
          }
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
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Unable to process frames. Transform repository was not properly initialized.");
        return;
      }

      std::shared_ptr<Network::IGTConnector> connection = m_networkSystem.GetConnection(m_connectionName);
      if (connection == nullptr)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Unable to process frames. Connection is not available.");
        return;
      }

      uint64 lastMessageId(std::numeric_limits<uint64>::max());
      while (!token.is_canceled())
      {
        std::unique_lock<std::mutex> lock(m_processorLock);
        if (m_videoFrameProcessor == nullptr || !connection->IsConnected())
        {
          lock.unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        MediaFrameReference^ cameraFrame(m_videoFrameProcessor->GetLatestFrame());
        l_latestTrackedFrame = connection->GetTrackedFrame(&m_latestTimestamp);
        if (l_latestTrackedFrame != nullptr &&
            cameraFrame != nullptr &&
            cameraFrame != l_latestCameraFrame)
        {
          float4x4 cameraToRawWorldAnchor = float4x4::identity();

          l_latestCameraFrame = cameraFrame;
          m_latestTimestamp = l_latestTrackedFrame->Timestamp;
          if (l_latestCameraFrame->CoordinateSystem != nullptr)
          {
            std::lock_guard<std::mutex> guard(m_anchorLock);
            Platform::IBox<float4x4>^ cameraToRawWorldAnchorBox;
            try
            {
              cameraToRawWorldAnchorBox = l_latestCameraFrame->CoordinateSystem->TryGetTransformTo(m_worldAnchor->RawCoordinateSystem);
            }
            catch (Platform::Exception^ e)
            {
              Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, L"Exception: " + e->Message);
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

          LandmarkRegistration::VecFloat3 sphereInReferenceResults;
          if (!RetrieveTrackerFrameLocations(l_latestTrackedFrame, sphereInReferenceResults))
          {
            continue;
          }

          VideoMediaFrame^ frame(nullptr);
          float4x4 phantomToCameraTransform;
          try
          {
            frame = l_latestCameraFrame->VideoMediaFrame;
          }
          catch (Platform::Exception^ e)
          {
            continue;
          }
          if (ComputePhantomToCameraTransform(frame, l_initialized, l_height, l_width, l_hsv, l_redMat, l_redMatWrap, l_imageRGB, l_mask, l_rvec, l_tvec, l_canny_output, phantomToCameraTransform))
          {
            // Transform points in model space to anchor space
            LandmarkRegistration::VecFloat3 sphereInAnchorResults;
            int i = 0;
            for (auto& sphereToPhantom : m_sphereToPhantomPoses)
            {
              float4x4 sphereToAnchorPose = sphereToPhantom * phantomToCameraTransform * cameraToRawWorldAnchor;

              sphereInAnchorResults.push_back(float3(sphereToAnchorPose.m41, sphereToAnchorPose.m42, sphereToAnchorPose.m43));

              if (m_visualizationEnabled)
              {
                // If visualizing, update the latest known poses of the spheres
                m_sphereToAnchorPoses[i] = sphereToAnchorPose;
                i++;
              }
            }

            std::lock_guard<std::mutex> frameGuard(m_outputFramesLock);
            m_sphereInAnchorResultFrames.push_back(sphereInAnchorResults);
            m_sphereInReferenceResultFrames.push_back(sphereInReferenceResults);

            if (lastMessageId != std::numeric_limits<uint64>::max())
            {
              m_notificationSystem.RemoveMessage(lastMessageId);
            }
            lastMessageId = m_notificationSystem.QueueMessage(L"Acquired " + m_sphereInAnchorResultFrames.size().ToString() + L" frame" + (m_sphereInAnchorResultFrames.size() > 1 ? L"s." : L"."));
          }
        }
        else
        {
          // No new frame
          lock.unlock();
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::PerformLandmarkRegistration(Concurrency::cancellation_token token)
    {


      while (!token.is_canceled())
      {
        uint32 size;
        {
          std::unique_lock<std::mutex> frameLock(m_outputFramesLock);
          if (m_sphereInAnchorResultFrames.size() < m_lastRegistrationResultCount + NUMBER_OF_FRAMES_BETWEEN_REGISTRATION)
          {
            frameLock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            continue;
          }

          assert(m_sphereInAnchorResultFrames.size() == m_sphereInReferenceResultFrames.size());

          // TODO : determine why astronomical values
          for (uint32 i = 0; i < m_sphereInReferenceResultFrames.size();)
          {
            auto ref = m_sphereInReferenceResultFrames[i];
            auto anc = m_sphereInAnchorResultFrames[i];

            bool remove(false);
            for (auto point : ref)
            {
              if (abs(point.x) > 100.0 || abs(point.y) > 100.0 || abs(point.z) > 100)
              {
                remove = true;
              }
            }
            for (auto point : anc)
            {
              if (abs(point.x) > 100.0 || abs(point.y) > 100.0 || abs(point.z) > 100)
              {
                remove = true;
              }
            }
            if (remove)
            {
              m_sphereInReferenceResultFrames.erase(m_sphereInReferenceResultFrames.begin() + i);
              m_sphereInAnchorResultFrames.erase(m_sphereInAnchorResultFrames.begin() + i);
            }
            else
            {
              ++i;
            }
          }
          m_landmarkRegistration->SetSourceLandmarks(m_sphereInReferenceResultFrames);
          m_landmarkRegistration->SetTargetLandmarks(m_sphereInAnchorResultFrames);

          size = m_sphereInAnchorResultFrames.size();
        }

        std::atomic_bool calcFinished(false);
        bool resultValid(false);
        auto start = std::chrono::system_clock::now();
        m_landmarkRegistration->CalculateTransformationAsync().then([this, &calcFinished, &resultValid](float4x4 referenceToAnchorTransformation)
        {
          if (referenceToAnchorTransformation == float4x4::identity())
          {
            resultValid = false;
          }
          else
          {
            m_hasRegistration = true;
            resultValid = true;
            m_referenceToAnchor = referenceToAnchorTransformation;
            if (m_completeCallback)
            {
              m_completeCallback(referenceToAnchorTransformation);
            }
          }
          calcFinished = true;
        });

        while (!calcFinished)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }

        m_lastRegistrationResultCount = size;

        auto end = std::chrono::system_clock::now();
        std::chrono::duration<double> diff = end - start;
        std::stringstream ss;
        ss << "Registration took " << diff.count() << "s.";
        Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, ss.str());
      }
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
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Phantom coordinates haven't been received. Can't determine 3D sphere coordinates.");
        return false;
      }

      CameraIntrinsics^ cameraIntrinsics(nullptr);
      try
      {
        if (videoFrame == nullptr || videoFrame->CameraIntrinsics == nullptr)
        {
          Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Camera intrinsics not available. Cannot continue.");
          return false;
        }
        cameraIntrinsics = videoFrame->CameraIntrinsics;
      }
      catch (Platform::Exception^ e)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
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
          cv::HoughCircles(mask, circles, CV_HOUGH_GRADIENT, 2, mask.rows / 16, 255, 30, 15, 60);

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
          else if (circles.size() > PHANTOM_SPHERE_COUNT)
          {
            // TODO : is it possible to make our code more robust by identifying 5 circles that make sense? pixel center distances? radii? etc...
            result = false;
            goto done;
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

          // Is this our first frame?
          if (m_pnpNeedsInit)
          {
            // Initialize rvec and tvec with a reasonable guess
            if (!cv::solvePnP(phantomFiducialsCv, circleCentersPixel, intrinsic, distCoeffs, rvec, tvec, false, cv::SOLVEPNP_DLS))
            {
              result = false;
              goto done;
            }
          }

          // Use iterative technique to refine results
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

          if (!IsPhantomToCameraSane(phantomToCameraTransform))
          {
            // Somethings gone wonky, let's try with a fresh start on the next frame
            m_pnpNeedsInit = true;
            return false;
          }

          return true;
        }
done:
        delete buffer;
        delete reference;
      }

      return result;
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::RetrieveTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, LandmarkRegistration::VecFloat3& outSphereInReferencePositions)
    {
      if (!m_transformRepository->SetTransforms(trackedFrame))
      {
        return false;
      }

      float4x4 sphereXToReferenceTransform[PHANTOM_SPHERE_COUNT];

      // Calculate world position from transforms in tracked frame
      try
      {
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          float4x4 transform;
          if (m_transformRepository->GetTransform(m_sphereCoordinateNames[i], &transform))
          {
            sphereXToReferenceTransform[i] = transpose(transform);
          }
        }
      }
      catch (Platform::Exception^ e)
      {
        return false;
      }

      float4 origin = { 0.f, 0.f, 0.f, 1.f };
      for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
      {
        outSphereInReferencePositions.push_back(float3(sphereXToReferenceTransform[i].m41, sphereXToReferenceTransform[i].m42, sphereXToReferenceTransform[i].m43));
      }

      // Phantom is rigid body, so only need to pull the values once
      if (!m_hasSphereToPhantomPoses)
      {
        bool hasError(false);
        for (int i = 0; i < PHANTOM_SPHERE_COUNT; ++i)
        {
          if (!ExtractPhantomToFiducialPose(m_sphereToPhantomPoses[i], m_transformRepository, L"RedSphere" + (i + 1).ToString(), L"Phantom"))
          {
            hasError = true;
          }
        }

        if (!hasError)
        {
          m_hasSphereToPhantomPoses = true;
        }
        else
        {
          return false;
        }
      }

      return true;
    }

    //----------------------------------------------------------------------------
    void CameraRegistration::OnAnchorRawCoordinateSystemAdjusted(Windows::Perception::Spatial::SpatialAnchor^ anchor, Windows::Perception::Spatial::SpatialAnchorRawCoordinateSystemAdjustedEventArgs^ args)
    {
      if (m_hasRegistration)
      {
        m_referenceToAnchor = m_referenceToAnchor * args->OldRawCoordinateSystemToNewRawCoordinateSystemTransform;
      }

      if (IsCameraActive())
      {
        // Registration is active, apply this to all m_rawWorldAnchorResults results, to adjust raw anchor coordinate system that they depend on
        std::lock_guard<std::mutex> frameGuard(m_outputFramesLock);
        for (auto& frame : m_sphereInAnchorResultFrames)
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
      typedef std::vector<uint32> Line; // indices into inCircles
      std::vector<Line> lines = NChooseR(PHANTOM_SPHERE_COUNT, 2);

      enum Colours
      {
        green_link = 0,
        blue_link,
        yellow_link,
        teal_link,
        colour_count
      };

      std::array<ColourToCircleList, colour_count> circleLinkResults;

      for (auto& line : lines)
      {
        // Given a pair of circles, compute histogram of patch that lies between, determine if profile of patch matches expected parameters
        auto firstCircle = inCircles[line[0]];
        auto secondCircle = inCircles[line[1]];
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
          circleLinkResults[blue_link].push_back(line[0]);
          circleLinkResults[blue_link].push_back(line[1]);
        }

        // Green hue, 45-65, tuning needed
        uint8 greenHue[2] = { 50, 70 };
        if (IsPatchColour(greenHue, 70, 40, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[green_link].push_back(line[0]);
          circleLinkResults[green_link].push_back(line[1]);
        }

        // Yellow hue, 17-37, tuning needed
        uint8 yelloHue[2] = { 17, 37 };
        if (IsPatchColour(yelloHue, 70, 50, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[yellow_link].push_back(line[0]);
          circleLinkResults[yellow_link].push_back(line[1]);
        }

        // Teal hue, 75-95, tuning needed
        uint8 tealHue[2] = { 75, 95 };
        if (IsPatchColour(tealHue, 70, 40, hsvMeans, histogram, pixelCount, FIFTH_PERCENTILE_FACTOR, MEAN_DISTRIBUTION_RATIO_THRESHOLD))
        {
          circleLinkResults[teal_link].push_back(line[0]);
          circleLinkResults[teal_link].push_back(line[1]);
        }
      }

      // If any pair of links has size 2, we can deduce the results
      auto& blueLinks = circleLinkResults[blue_link];
      auto& greenLinks = circleLinkResults[green_link];
      auto& yellowLinks = circleLinkResults[yellow_link];
      auto& tealLinks = circleLinkResults[teal_link];
      if (!((blueLinks.size() == 2 && greenLinks.size() == 2 && tealLinks.size() == 2) ||
            (blueLinks.size() == 2 && yellowLinks.size() == 2 && tealLinks.size() == 2) ||
            (greenLinks.size() == 2 && yellowLinks.size() == 2 && tealLinks.size() == 2) ||
            (greenLinks.size() == 2 && yellowLinks.size() == 2 && blueLinks.size() == 2)))
      {
        // No trio of 2, cannot deduce pattern
        return false;
      }

      // Determine valid triplet
      std::vector<uint32>* listA(nullptr);
      std::vector<uint32>* listB(nullptr);
      std::vector<uint32>* listC(nullptr);
      if (blueLinks.size() == 2 && yellowLinks.size() == 2 && tealLinks.size() == 2)
      {
        listA = &blueLinks;
        listB = &yellowLinks;
        listC = &tealLinks;
      }
      else if (blueLinks.size() == 2 && greenLinks.size() == 2 && tealLinks.size() == 2)
      {
        listA = &blueLinks;
        listB = &greenLinks;
        listC = &tealLinks;
      }
      else if (greenLinks.size() == 2 && yellowLinks.size() == 2 && blueLinks.size() == 2)
      {
        listA = &greenLinks;
        listB = &yellowLinks;
        listC = &blueLinks;
      }
      else
      {
        listA = &tealLinks;
        listB = &greenLinks;
        listC = &yellowLinks;
      }

      // If this is a successful detection, there will be one and only one index common to listA, listB, and listC
      int32 centerSphereIndex(-1);
      for (auto& circleIndex : *listA)
      {
        auto listBIter = std::find(listB->begin(), listB->end(), circleIndex);
        auto listCIter = std::find(listC->begin(), listC->end(), circleIndex);
        if (listBIter != listB->end() && listCIter != listC->end())
        {
          centerSphereIndex = circleIndex;
          break;
        }
      }

      if (centerSphereIndex == -1)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "No index common to all lists.");
        return false;
      }

      // Find it, remove it from the other lists, and the values remaining in those lists are the index of the other circles
      RemoveResultFromList(*listA, centerSphereIndex);
      RemoveResultFromList(*listB, centerSphereIndex);
      RemoveResultFromList(*listC, centerSphereIndex);

      // Now we know one, and two others (based on which colour of list they're in)
      std::vector<cv::Point3f> output(5);

      output[centerSphereIndex] = inOutPhantomFiducialsCv[1];

      std::vector<uint32> remainingColours = { 0, 2, 3, 4 }; // 0 = green, 2 = yellow, 3 = blue, 4 = teal
      std::vector<uint32> remainingIndicies = { 0, 1, 2, 3, 4 };

      remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), centerSphereIndex));

      if (listA == &greenLinks || listB == &greenLinks || listC == &greenLinks)
      {
        output[greenLinks[0]] = inOutPhantomFiducialsCv[0];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), greenLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 0));
      }
      if (listA == &blueLinks || listB == &blueLinks || listC == &blueLinks)
      {
        output[blueLinks[0]] = inOutPhantomFiducialsCv[3];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), blueLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 3));
      }
      if (listA == &yellowLinks || listB == &yellowLinks || listC == &yellowLinks)
      {
        output[yellowLinks[0]] = inOutPhantomFiducialsCv[2];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), yellowLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 2));
      }
      if (listA == &tealLinks || listB == &tealLinks || listC == &tealLinks)
      {
        output[tealLinks[0]] = inOutPhantomFiducialsCv[4];
        remainingIndicies.erase(std::find(remainingIndicies.begin(), remainingIndicies.end(), tealLinks[0]));
        remainingColours.erase(std::find(remainingColours.begin(), remainingColours.end(), 4));
      }

      // One remaining circle has not yet been set, it's index (as per remaining colours above) remains
      assert(remainingColours.size() == 1 && remainingIndicies.size() == 1);

      // Fill the last remaining index with the last remaining colour
      output[remainingIndicies[0]] = inOutPhantomFiducialsCv[remainingColours[0]];

      inOutPhantomFiducialsCv = output;

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

    //----------------------------------------------------------------------------
    void CameraRegistration::Init()
    {
      try
      {
        InitializeTransformRepositoryAsync(L"configuration.xml", m_configStorageFolder, m_transformRepository).then([this]()
        {
          m_transformsAvailable = true;
        });
      }
      catch (Platform::Exception^ e)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
      }

      try
      {
        LoadXmlDocumentAsync(L"configuration.xml", m_configStorageFolder).then([this](XmlDocument ^ doc)
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/CameraRegistration");
          if (doc->SelectNodes(xpath)->Length == 0)
          {
            throw ref new Platform::Exception(E_INVALIDARG, L"No camera registration defined in the configuration file.");
          }

          for (auto node : doc->SelectNodes(xpath))
          {
            // model, transform
            if (node->Attributes->GetNamedItem(L"IGTConnection") == nullptr)
            {
              throw ref new Platform::Exception(E_FAIL, L"Camera registration entry does not contain IGTConnection attribute.");
            }
            Platform::String^ igtConnectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
            if (igtConnectionName != nullptr)
            {
              m_connectionName = std::wstring(begin(igtConnectionName), end(igtConnectionName));
            }
          }
        });
      }
      catch (Platform::Exception^ e)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, "Unable to read configuration file.");
      }

      m_sphereCoordinateNames[0] = ref new UWPOpenIGTLink::TransformName(L"RedSphere1", L"Reference");
      m_sphereCoordinateNames[1] = ref new UWPOpenIGTLink::TransformName(L"RedSphere2", L"Reference");
      m_sphereCoordinateNames[2] = ref new UWPOpenIGTLink::TransformName(L"RedSphere3", L"Reference");
      m_sphereCoordinateNames[3] = ref new UWPOpenIGTLink::TransformName(L"RedSphere4", L"Reference");
      m_sphereCoordinateNames[4] = ref new UWPOpenIGTLink::TransformName(L"RedSphere5", L"Reference");
    }

    //----------------------------------------------------------------------------
    bool CameraRegistration::IsPhantomToCameraSane(const float4x4& phantomToCameraTransform)
    {
      // This function contains all the rules for what validates a "sane" phantomToCamera (phantom pose)

      // Is Z < 0 (forward?), is it more than ... 15cm away?
      if (phantomToCameraTransform.m43 > -0.15f)
      {
        return false;
      }

      // X, Y, less than 1.3m from center?
      if (fabs(phantomToCameraTransform.m42) > 1.3f || fabs(phantomToCameraTransform.m41) > 1.3f)
      {
        return false;
      }

      return true;
    }
  }
}