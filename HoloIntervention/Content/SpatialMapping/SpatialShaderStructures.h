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

using namespace DirectX;

namespace HoloIntervention
{
  namespace Spatial
  {
    struct VertexBufferType
    {
      XMFLOAT4 vertex;
    };

    struct IndexBufferType
    {
      uint32 index;
    };

    struct OutputBufferType
    {
      XMFLOAT4 intersectionPoint;
      XMFLOAT4 intersectionNormal;
    };

    struct WorldConstantBuffer
    {
      // Constant buffers must have a a ByteWidth multiple of 16
      DirectX::XMFLOAT4X4 meshToWorld;
    };

    struct RayConstantBuffer
    {
      XMFLOAT4 rayOrigin;
      XMFLOAT4 rayDirection;
    };

    static_assert((sizeof(WorldConstantBuffer) % (sizeof(float) * 4)) == 0, "World constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");
    static_assert((sizeof(RayConstantBuffer) % (sizeof(float) * 4)) == 0, "Ray constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");
  }
}