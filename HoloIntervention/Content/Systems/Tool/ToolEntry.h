/*====================================================================
Copyright(c) 2018 Adam Rankin


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

// STL includes
#include <atomic>

// OpenCV
#include <Opencv2/core/mat.hpp>

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace UI
  {
    class IconEntry;
    class Icons;
  }

  namespace System
  {
    class NetworkSystem;
  }

  namespace Rendering
  {
    class ModelEntry;
    class ModelRenderer;
  }

  namespace Algorithm
  {
    class KalmanFilter;
  }

  namespace Tools
  {
    class ToolEntry : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      ToolEntry(Rendering::ModelRenderer& modelRenderer,
                System::NetworkSystem& networkSystem,
                UI::Icons& icons,
                uint64 hashedConnectionName,
                UWPOpenIGTLink::TransformName^ coordinateFrame,
                UWPOpenIGTLink::TransformRepository^ transformRepository,
                Platform::String^ userId);
      ToolEntry(Rendering::ModelRenderer& modelRenderer,
                System::NetworkSystem& networkSystem,
                UI::Icons& icons,
                uint64 hashedConnectionName,
                const std::wstring& coordinateFrame,
                UWPOpenIGTLink::TransformRepository^ transformRepository,
                Platform::String^ userId);
      ~ToolEntry();

      void Update(const DX::StepTimer& timer);

      Concurrency::task<void> SetModelEntryAsync(std::shared_ptr<Rendering::ModelEntry> entry);
      std::shared_ptr<Rendering::ModelEntry> GetModelEntry();
      UWPOpenIGTLink::TransformName^ GetCoordinateFrame() const;
      bool IsValid() const;
      bool WasValid() const;

      uint64 GetId() const;
      std::wstring GetUserId() const;

      void SetHiddenOverride(bool arg);

    protected:
      // Cached links to system resources
      Rendering::ModelRenderer&                   m_modelRenderer;
      System::NetworkSystem&                      m_networkSystem;
      UI::Icons&                                  m_icons;
      UWPOpenIGTLink::TransformRepository^        m_transformRepository;
      uint64                                      m_hashedConnectionName;

      std::atomic_bool                            m_isValid = false;
      std::atomic_bool                            m_wasValid = false;
      UWPOpenIGTLink::TransformName^              m_coordinateFrame;
      std::shared_ptr<Rendering::ModelEntry>      m_modelEntry = nullptr;
      double                                      m_latestTimestamp = 0.0;
      std::wstring                                m_userId;
      std::atomic_bool                            m_hiddenOverride = false;

      // Icon details
      std::shared_ptr<UI::IconEntry>              m_iconEntry = nullptr;

      // Kalman filter for smoothing and prediction
      std::shared_ptr<Algorithm::KalmanFilter>    m_kalmanFilter = nullptr;
      std::atomic_bool                            m_firstDataPoint = true;
      cv::Mat                                     m_correctionMatrix = cv::Mat(7, 1, CV_32F);
    };
  }
}