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

// Local includes
#include "pch.h"
#include "Rendering\RenderingCommon.h"

using namespace DirectX;
using namespace Windows::Foundation::Numerics;

namespace Valhalla
{
  //----------------------------------------------------------------------------
  bool IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum, const std::vector<float3>& bounds)
  {
    // For each plane, check to see if all 8 points are in front, if so, obj is outside
    for(auto& entry :
        {
          frustum.Left, frustum.Right, frustum.Bottom, frustum.Top, frustum.Near, frustum.Far
        })
    {
      XMVECTOR plane = XMLoadPlane(&entry);

      bool objFullyInFront(true);

      for(auto& point : bounds)
      {
        XMVECTOR dotProduct = XMPlaneDotCoord(plane, XMLoadFloat3(&point));

        if(XMVectorGetX(dotProduct) < 0.f)
        {
          objFullyInFront = false;
          break;
        }
      }

      if(objFullyInFront)
      {
        return false;
      }
    }

    return true;
  }
}

//----------------------------------------------------------------------------
DirectX::XMFLOAT4 operator-(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
}

//----------------------------------------------------------------------------
DirectX::XMFLOAT4 operator+(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
}

//----------------------------------------------------------------------------
DirectX::XMFLOAT4 operator*(const float& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z, lhs * rhs.w);
}

//----------------------------------------------------------------------------
DirectX::XMFLOAT4 operator*(const DirectX::XMFLOAT4& lhs, const float& rhs)
{
  return DirectX::XMFLOAT4(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
}
