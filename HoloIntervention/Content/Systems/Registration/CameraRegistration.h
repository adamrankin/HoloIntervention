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
#include "IStabilizedComponent.h"
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
  namespace Rendering
  {
    class PrimitiveEntry;
    class ModelRenderer;
  }

  namespace Network
  {
    class IGTConnector;
  }

  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;

    class CameraRegistration : public IStabilizedComponent
    {
      typedef std::vector<uint32> ColourToCircleList;

      enum State
      {
        Stopped,
        Initializing,
        Initialized,
        Recording,
      };

      struct HsvHistogram
      {
        std::array<uint32, 180> hue = { 0 };
        std::array<uint32, 256> saturation = { 0 };
        std::array<uint32, 256> value = { 0 };
      };

    public:
      typedef std::vector<Windows::Foundation::Numerics::float2> VecFloat2;
      typedef std::vector<Windows::Foundation::Numerics::float3> VecFloat3;
      typedef std::vector<Windows::Foundation::Numerics::float4> VecFloat4;
      typedef std::vector<Windows::Foundation::Numerics::float4x4> VecFloat4x4;
      typedef std::vector<VecFloat3> DetectionFrames;

    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      CameraRegistration(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer);
      ~CameraRegistration();

      void Update(Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToRequestedBox);
      Concurrency::task<bool> StopCameraAsync();
      Concurrency::task<bool> StartCameraAsync();
      bool IsCameraActive() const;
      void SetVisualization(bool enabled);

      Windows::Perception::Spatial::SpatialAnchor^ GetWorldAnchor();
      void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor);

      bool HasRegistration() const;
      Windows::Foundation::Numerics::float4x4 GetReferenceToWorldAnchorTransformation() const;

      void RegisterCompletedCallback(std::function<void(Windows::Foundation::Numerics::float4x4)> function);

    protected:
      void Init();

      bool IsPhantomToCameraSane(const Windows::Foundation::Numerics::float4x4& phantomToCameraTransform);
      void ProcessAvailableFrames(Concurrency::cancellation_token token);
      void PerformLandmarkRegistration();
      bool CameraRegistration::RetrieveTrackerFrameLocations(UWPOpenIGTLink::TrackedFrame^ trackedFrame, VecFloat3& outSphereInReferencePositions);
      bool ComputePhantomToCameraTransform(Windows::Media::Capture::Frames::VideoMediaFrame^ videoFrame, bool& initialized, int32_t& height, int32_t& width,
                                           cv::Mat& hsv, cv::Mat& redMat, cv::Mat& redMatWrap, cv::Mat& imageRGB, cv::Mat& mask, cv::Mat& rvec,
                                           cv::Mat& tvec, cv::Mat& cannyOutput, Windows::Foundation::Numerics::float4x4& modelToCameraTransform);
      void OnAnchorRawCoordinateSystemAdjusted(Windows::Perception::Spatial::SpatialAnchor^ anchor, Windows::Perception::Spatial::SpatialAnchorRawCoordinateSystemAdjustedEventArgs^ args);
      bool SortCorrespondence(cv::Mat& image, std::vector<cv::Point3f>& inOutPhantomFiducialsCv, const std::vector<cv::Point3f>& inCircles);
      inline void CalculatePatchHistogramHSV(const Windows::Foundation::Numerics::float2& startPixel,
                                             const Windows::Foundation::Numerics::float2& endPixel,
                                             const Windows::Foundation::Numerics::float2& atVector,
                                             const Windows::Foundation::Numerics::float2& tangentVector, const float tangentPixelCount,
                                             byte* imageData, const cv::MatStep& step, HsvHistogram& outHSVHistogram,
                                             std::array<uint32, 3>& hsvMeans, uint32& outPixelCount);
      inline uint32 CalculatePixelValue(const Windows::Foundation::Numerics::float2& currentPixelLocation, byte* imageData, const cv::MatStep& step);
      inline bool IsPatchColour(const uint8 hueRange[2], const uint8 saturationMin, const uint8 valueMin, const std::array<uint32, 3>& hsvMeans, const HsvHistogram& histogram,
                                const uint32 pixelCount, const float percentileFactor, const float distributionRatio);
      inline void RemoveResultFromList(ColourToCircleList& circleLinkResult, int32 centerSphereIndex);

    protected:
      // Cached entries
      Rendering::ModelRenderer&                                             m_modelRenderer;
      NotificationSystem&                                                   m_notificationSystem;
      NetworkSystem&                                                        m_networkSystem;

      std::function<void(Windows::Foundation::Numerics::float4x4)>          m_completeCallback;
      mutable std::mutex                                                    m_processorLock;
      std::shared_ptr<Capture::VideoFrameProcessor>                         m_videoFrameProcessor = nullptr;

      // Anchor resources
      std::mutex                                                            m_anchorMutex;
      Windows::Perception::Spatial::SpatialAnchor^                          m_worldAnchor = nullptr;

      // Visualization resources
      std::atomic_bool                                                      m_visualizationEnabled = false;
      std::array<uint64, 5>                                                 m_spherePrimitiveIds = { 0 };
      std::array<std::shared_ptr<Rendering::PrimitiveEntry>, 5>             m_spherePrimitives = { nullptr };
      std::array<Windows::Foundation::Numerics::float4x4, 5>                m_sphereToAnchorPoses;

      // Camera
      std::atomic_bool                                                      m_cameraActive = false;
      Windows::Foundation::EventRegistrationToken                           m_anchorUpdatedToken;
      std::mutex                                                            m_framesLock;
      Windows::Media::Capture::Frames::MediaFrameReference^                 m_currentFrame = nullptr;
      Windows::Media::Capture::Frames::MediaFrameReference^                 m_nextFrame = nullptr;
      DetectionFrames                                                       m_sphereInAnchorResultFrames;
      uint32                                                                m_captureFrameCount = 20;

      // IGT link
      std::wstring                                                          m_connectionName;
      UWPOpenIGTLink::TransformRepository^                                  m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      std::atomic_bool                                                      m_transformsAvailable = false;
      double                                                                m_latestTimestamp = 0.0;
      DetectionFrames                                                       m_sphereInReferenceResultFrames;
      std::array<Windows::Foundation::Numerics::float4x4, 5>                m_sphereToPhantomPoses;
      std::atomic_bool                                                      m_hasSphereToPhantomPoses = false;
      std::array<UWPOpenIGTLink::TransformName^, 5>                         m_sphereCoordinateNames;

      // State variables
      Concurrency::cancellation_token_source                                m_tokenSource;
      std::atomic_bool                                                      m_hasRegistration = false;
      std::atomic_bool                                                      m_pnpNeedsInit = true;

      Windows::Foundation::Numerics::float4x4                               m_referenceToAnchor = Windows::Foundation::Numerics::float4x4::identity(); // column-major order
      std::shared_ptr<LandmarkRegistration>                                 m_landmarkRegistration = std::make_shared<LandmarkRegistration>();

      static const uint32                                                   PHANTOM_SPHERE_COUNT = 5;
      static const float                                                    VISUALIZATION_SPHERE_RADIUS;
    };
  }
}