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

// stl includes
#include <future>
#include <memory>

using namespace Windows::Perception::Spatial;

namespace DX
{
  class DeviceResources;
}

namespace HoloIntervention
{
  namespace System
  {
    class CameraRegistration : public Sound::IVoiceInput
    {
      enum State
      {
        Stopped,
        Initializing,
        Initialized,
        Recording,
      };
    public:
      CameraRegistration(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~CameraRegistration();

      void Update(SpatialCoordinateSystem^ coordinateSystem);

      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks);

    protected:
      void ProcessAvailableFrames(cancellation_token token);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                  m_deviceResources;
      std::mutex                                            m_processorLock;
      std::shared_ptr<Capture::VideoFrameProcessor>         m_videoFrameProcessor = nullptr;
      task<std::shared_ptr<Capture::VideoFrameProcessor>>*  m_createTask = nullptr;

      SpatialCoordinateSystem^                              m_worldCoordinateSystem = nullptr;
      std::mutex                                            m_framesLock;
      Windows::Media::Capture::Frames::MediaFrameReference^ m_currentFrame = nullptr;
      Windows::Media::Capture::Frames::MediaFrameReference^ m_nextFrame = nullptr;

      Concurrency::task<void>*                              m_workerTask = nullptr;
      Concurrency::cancellation_token_source                m_tokenSource;
      Windows::Foundation::Numerics::float4x4               m_cameraToWorld = Windows::Foundation::Numerics::float4x4::identity();

      std::shared_ptr<LandmarkRegistration>                 m_landmarkRegistration = std::make_shared<LandmarkRegistration>();
    };
  }
}