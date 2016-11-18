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

#pragma once

// Local includes
#include "LandmarkRegistration.h"

// Capture includes
#include "VideoFrameProcessor.h"

// Sound includes
#include "IVoiceInput.h"

// OpenCV includes
#include <opencv2/core.hpp>

// STL includes
#include <future>
#include <memory>

// WinRT includes
#include <MemoryBuffer.h>

namespace DX
{
  class DeviceResources;
}

namespace DirectX
{
  class InstancedGeometricPrimitive;
}

namespace HoloIntervention
{
  namespace System
  {
    class CameraRegistration
    {
      enum State
      {
        Stopped,
        Initializing,
        Initialized,
        Recording,
      };

    public:
      typedef std::vector<Windows::Foundation::Numerics::float2> VecFloat2;
      typedef std::vector<Windows::Foundation::Numerics::float3> VecFloat3;
      typedef std::vector<Windows::Foundation::Numerics::float4> VecFloat4;
      typedef std::vector<VecFloat3> DetectionFrames;

    public:
      CameraRegistration(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~CameraRegistration();

      void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem, Platform::IBox<Windows::Foundation::Numerics::float4x4>^ worldAnchorToRequestedBox);
      Concurrency::task<bool> StopCameraAsync();
      Concurrency::task<bool> StartCameraAsync();
      void SetVisualization(bool enabled);

      Windows::Perception::Spatial::SpatialAnchor^ GetWorldAnchor();
      void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor);

      bool HasRegistration() const;
      Windows::Foundation::Numerics::float4x4 GetTrackerToWorldAnchorTransformation() const;

    protected:
      void ProcessAvailableFrames(Concurrency::cancellation_token token);
      bool CameraRegistration::RetrieveTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, VecFloat3& worldResults);

      bool ComputePhantomToCameraTransform(Windows::Media::Capture::Frames::VideoMediaFrame^ videoFrame, bool& initialized, int32_t& height, int32_t& width,
                                           cv::Mat& hsv, cv::Mat& redMat, cv::Mat& redMatWrap, cv::Mat& imageRGB, cv::Mat& mask, cv::Mat& cannyOutput, Windows::Foundation::Numerics::float4x4& modelToCameraTransform);
      void OnAnchorRawCoordinateSystemAdjusted(Windows::Perception::Spatial::SpatialAnchor^ anchor, Windows::Perception::Spatial::SpatialAnchorRawCoordinateSystemAdjustedEventArgs^ args);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                                  m_deviceResources;
      std::mutex                                                            m_processorLock;
      std::shared_ptr<Capture::VideoFrameProcessor>                         m_videoFrameProcessor = nullptr;
      Concurrency::task<std::shared_ptr<Capture::VideoFrameProcessor>>*     m_createTask = nullptr;

      // Anchor resources
      std::mutex                                                            m_anchorMutex;
      Windows::Perception::Spatial::SpatialAnchor^                          m_worldAnchor = nullptr;

      // Visualization resources
      std::atomic_bool                                                      m_visualizationEnabled = false;
      std::array<uint64, 5>                                                 m_spherePrimitiveIds = { 0 };
      std::array<Windows::Foundation::Numerics::float4x4, 5>                m_sphereToCamera;

      // Camera
      Windows::Foundation::EventRegistrationToken                           m_anchorUpdatedToken;
      std::mutex                                                            m_framesLock;
      Windows::Media::Capture::Frames::MediaFrameReference^                 m_currentFrame = nullptr;
      Windows::Media::Capture::Frames::MediaFrameReference^                 m_nextFrame = nullptr;
      DetectionFrames                                                       m_rawWorldAnchorResults;

      // IGT link
      UWPOpenIGTLink::TransformRepository^                                  m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      std::atomic_bool                                                      m_transformsAvailable = false;
      double                                                                m_latestTimestamp = 0.0;
      DetectionFrames                                                       m_trackerFrameResults;
      VecFloat4                                                             m_phantomFiducialCoords;
      std::array<UWPOpenIGTLink::TransformName^, 5>                         m_sphereCoordinateNames;

      // State variables
      Concurrency::task<void>*                                              m_workerTask = nullptr;
      Concurrency::cancellation_token_source                                m_tokenSource;
      std::atomic_bool                                                      m_hasRegistration = false;

      Windows::Foundation::Numerics::float4x4                               m_trackerToWorldAnchor = Windows::Foundation::Numerics::float4x4::identity(); // row-major order
      std::shared_ptr<LandmarkRegistration>                                 m_landmarkRegistration = std::make_shared<LandmarkRegistration>();

      static const uint32                                                   PHANTOM_SPHERE_COUNT = 5;
      static const uint32                                                   NUMBER_OF_FRAMES_FOR_CALIBRATION = 30;
      static const float                                                    VISUALIZATION_SPHERE_RADIUS;
    };
  }
}