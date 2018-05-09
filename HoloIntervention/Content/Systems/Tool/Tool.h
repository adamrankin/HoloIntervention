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
    class Icon;
    class Icons;
  }

  namespace System
  {
    class NetworkSystem;
  }

  namespace Rendering
  {
    class Model;
    class ModelRenderer;
  }

  namespace Algorithm
  {
    class KalmanFilter;
  }

  namespace Tools
  {
    class Tool : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      Tool(Rendering::ModelRenderer& modelRenderer,
           System::NetworkSystem& networkSystem,
           UI::Icons& icons,
           uint64 hashedConnectionName,
           UWPOpenIGTLink::TransformName^ coordinateFrame,
           UWPOpenIGTLink::TransformRepository^ transformRepository,
           Platform::String^ userId);
      Tool(Rendering::ModelRenderer& modelRenderer,
           System::NetworkSystem& networkSystem,
           UI::Icons& icons,
           uint64 hashedConnectionName,
           const std::wstring& coordinateFrame,
           UWPOpenIGTLink::TransformRepository^ transformRepository,
           Platform::String^ userId);
      ~Tool();

      void Update(const DX::StepTimer& timer);

      Concurrency::task<void> SetModelAsync(std::shared_ptr<Rendering::Model> entry);
      std::shared_ptr<Rendering::Model> GetModel();

      void SetCoordinateFrame(UWPOpenIGTLink::TransformName^ coordFrame);
      UWPOpenIGTLink::TransformName^ GetCoordinateFrame() const;

      bool IsValid() const;
      bool WasValid() const;

      void SetModelToObjectTransform(Windows::Foundation::Numerics::float4x4 transform);
      Windows::Foundation::Numerics::float4x4 GetModelToObjectTransform() const;

      uint64 GetId() const;
      std::wstring GetUserId() const;

      void SetHiddenOverride(bool arg);

      void ShowIcon(bool show);

    protected:
      // Cached links to system resources
      Rendering::ModelRenderer&                   m_modelRenderer;
      System::NetworkSystem&                      m_networkSystem;
      UI::Icons&                                  m_icons;

      // Tool state
      std::wstring                                m_userId;
      uint64                                      m_hashedConnectionName;
      double                                      m_latestTimestamp = 0.0;
      UWPOpenIGTLink::TransformRepository^        m_transformRepository;
      UWPOpenIGTLink::TransformName^              m_coordinateFrame;

      // Model details
      std::atomic_bool                            m_isValid = false;
      std::atomic_bool                            m_wasValid = false;
      std::shared_ptr<Rendering::Model>           m_modelEntry = nullptr;
      std::atomic_bool                            m_hiddenOverride = false;
      Windows::Foundation::Numerics::float4x4     m_modelToObjectTransform = Windows::Foundation::Numerics::float4x4::identity(); // Column major

      // Icon details
      std::shared_ptr<UI::Icon>                   m_iconEntry = nullptr;

      // Coordinate frame details
      static Platform::String^                    MODEL_COORDINATE_FRAME_NAME;
    };
  }
}