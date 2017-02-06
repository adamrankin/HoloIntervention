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
#include "CameraResources.h"
#include "PrimitiveEntry.h"
#include "StepTimer.h"

// Unnecessary, but reduces intellisense errors
#include <WindowsNumerics.h>

using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    const float PrimitiveEntry::PRIMITIVE_LERP_RATE = 4.f;

    //----------------------------------------------------------------------------
    PrimitiveEntry::PrimitiveEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive)
      : m_deviceResources(deviceResources)
      , m_primitive(std::move(primitive))
    {

    }

    //----------------------------------------------------------------------------
    PrimitiveEntry::~PrimitiveEntry()
    {
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::Update(const DX::StepTimer& timer, const DX::CameraResources* cameraResources)
    {
      m_cameraResources = cameraResources;

      const float& deltaTime = static_cast<float>(timer.GetElapsedSeconds());

      const float4x4 smoothedPosition = lerp(m_currentPose, m_desiredPose, deltaTime * PRIMITIVE_LERP_RATE);
      m_currentPose = smoothedPosition;

      const float3 deltaPosition = transform(float3(0.f, 0.f, 0.f), m_currentPose - m_lastPose); // meters
      m_velocity = deltaPosition * (1.f / deltaTime); // meters per second
      m_lastPose = m_currentPose;
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::Render()
    {
      if (m_visible)
      {
        FXMMATRIX view[2] =
        {
          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().view[0])),
          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().view[1]))
        };
        FXMMATRIX projection[2] =
        {
          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[0])),
          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[1]))
        };
        m_primitive->Draw(XMLoadFloat4x4(&m_currentPose), view, projection, XMLoadFloat4(&m_colour));
      }
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::SetVisible(bool enable)
    {
      m_visible = enable;
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::ToggleVisible()
    {
      m_visible = !m_visible;
    }

    //----------------------------------------------------------------------------
    bool PrimitiveEntry::IsVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    bool PrimitiveEntry::IsInFrustum(const SpatialBoundingFrustum& frustum) const
    {
      auto bounds = GetBounds();
      std::array<float3, 8> points
      {
        float3(bounds[0], bounds[2], bounds[4]),
        float3(bounds[1], bounds[2], bounds[4]),
        float3(bounds[0], bounds[2], bounds[4]),
        float3(bounds[1], bounds[2], bounds[5]),
        float3(bounds[0], bounds[3], bounds[4]),
        float3(bounds[1], bounds[3], bounds[4]),
        float3(bounds[0], bounds[3], bounds[4]),
        float3(bounds[1], bounds[3], bounds[5])
      };

      bool inside(true);
      for (auto& point : points)
      {
        XMVECTOR transformedPoint = XMLoadFloat3(&transform(point, m_currentPose));

        // check if point inside frustum (behind all planes)
        for (auto& entry : { frustum.Left, frustum.Right, frustum.Bottom, frustum.Top, frustum.Near, frustum.Far })
        {
          XMVECTOR plane = XMLoadPlane(&entry);
          XMVECTOR dotProduct = XMPlaneDotCoord(plane, transformedPoint);
          bool isBehind = XMVectorGetX(dotProduct) < 0.f;
          inside &= isBehind;
        }
      }

      return inside;
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::SetColour(float3 newColour)
    {
      m_colour = float4(newColour.x, newColour.y, newColour.z, 1.f);
    }

    //----------------------------------------------------------------------------
    float3 PrimitiveEntry::GetColour() const
    {
      return float3(m_colour.x, m_colour.y, m_colour.z);
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::SetDesiredPose(const float4x4& world)
    {
      m_desiredPose = world;
    }

    //----------------------------------------------------------------------------
    const float4x4& PrimitiveEntry::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    const float3& PrimitiveEntry::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    const std::array<float, 6>& PrimitiveEntry::GetBounds() const
    {
      return m_primitive->GetBounds();
    }

    //----------------------------------------------------------------------------
    uint64 PrimitiveEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void PrimitiveEntry::SetId(uint64 id)
    {
      m_id = id;
    }
  }
}