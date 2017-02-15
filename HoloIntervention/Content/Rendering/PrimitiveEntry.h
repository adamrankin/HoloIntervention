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
#include "InstancedEffectFactory.h"
#include "InstancedEffects.h"
#include "InstancedGeometricPrimitive.h"

// STL includes
#include <atomic>

namespace DirectX
{
  class InstancedGeometricPrimitive;
}

namespace DX
{
  class CameraResources;
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class PrimitiveEntry
    {
    public:
      PrimitiveEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive, DX::StepTimer& timer);
      ~PrimitiveEntry();

      void Update(const DX::CameraResources* cameraResources);
      void Render();

      // Primitive enable control
      void SetVisible(bool enable);
      void ToggleVisible();
      bool IsVisible() const;
      bool IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum) const;

      // Colour control
      void SetColour(Windows::Foundation::Numerics::float3 newColour);
      Windows::Foundation::Numerics::float3 GetColour() const;

      // Primitive pose control
      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& world);
      const Windows::Foundation::Numerics::float4x4& GetCurrentPose() const;

      const Windows::Foundation::Numerics::float3& GetVelocity() const;

      const std::array<float, 6>& GetBounds() const;

      // Accessors
      uint64 GetId() const;
      void SetId(uint64 id);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                  m_deviceResources;
      const DX::CameraResources*                            m_cameraResources = nullptr;
      DX::StepTimer&                                        m_timer;

      // Frustum checking
      mutable std::atomic_bool                              m_isInFrustum;
      mutable uint64                                        m_frustumCheckFrameNumber;

      // Primitive resources
      std::unique_ptr<DirectX::InstancedGeometricPrimitive> m_primitive = nullptr;
      Windows::Foundation::Numerics::float4                 m_colour;
      Windows::Foundation::Numerics::float3                 m_velocity;
      Windows::Foundation::Numerics::float4x4               m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4               m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4               m_currentPose = Windows::Foundation::Numerics::float4x4::identity();

      // Model related behavior
      std::atomic_bool                                      m_visible = false;
      uint64                                                m_id = INVALID_TOKEN;

      // Variables used with the rendering loop.
      std::atomic_bool                                      m_loadingComplete = false;

      static const float                                    PRIMITIVE_LERP_RATE;
    };
  }
}