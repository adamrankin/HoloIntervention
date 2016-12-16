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

// DirectX includes
#include <DirectXMath.h>

inline DirectX::XMFLOAT4 operator-(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w);
}

inline DirectX::XMFLOAT4 operator+(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w);
}

inline DirectX::XMFLOAT4 operator*(const float& lhs, const DirectX::XMFLOAT4& rhs)
{
  return DirectX::XMFLOAT4(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z, lhs * rhs.w);
}

inline DirectX::XMFLOAT4 operator*(const DirectX::XMFLOAT4& lhs, const float& rhs)
{
  return DirectX::XMFLOAT4(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs);
}