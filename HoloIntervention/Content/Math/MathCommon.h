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

// OpenCV includes
#include <opencv2/core.hpp>

// STL includes
#include <array>

namespace HoloIntervention
{
  typedef Windows::Foundation::Numerics::float3 Point;
  typedef Windows::Foundation::Numerics::float3 Vector3;
  typedef std::pair<Point, Vector3> Line;
  typedef std::pair<Point, Vector3> Plane;

  bool OpenCVToFloat4x4(const cv::Mat& inRotationMatrix, const cv::Mat& inTranslationMatrix, Windows::Foundation::Numerics::float4x4& outMatrix);
  bool OpenCVToFloat4x4(const cv::InputArray& inMatrix, Windows::Foundation::Numerics::float4x4& outMatrix);
  bool Float4x4ToOpenCV(const Windows::Foundation::Numerics::float4x4& inMatrix, cv::Mat& outMatrix);
  bool Float4x4ToOpenCV(const Windows::Foundation::Numerics::float4x4& inMatrix, cv::Mat& outRotation, cv::Mat& outTranslation);

  bool Float4x4ToArray(const Windows::Foundation::Numerics::float4x4& inMatrix, float outMatrix[16]);
  bool Float4x4ToArray(const Windows::Foundation::Numerics::float4x4& inMatrix, std::array<float, 16> outMatrix);
  bool ArrayToFloat4x4(const float* inMatrix, uint32 matrixSize, Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const float (&inMatrix)[16], Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const float(&inMatrix)[4][4], Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const std::array<float, 16>& inMatrix, Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const float (&inMatrix)[9], Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const float(&inMatrix)[3][3], Windows::Foundation::Numerics::float4x4& outMatrix);
  bool ArrayToFloat4x4(const std::array<float, 9>& inMatrix, Windows::Foundation::Numerics::float4x4& outMatrix);

  std::wstring PrintMatrix(const Windows::Foundation::Numerics::float4x4& matrix);

  /*
  Copyright (c) Elvis C. S. Chen, elvis.chen@gmail.com

  Use, modification and redistribution of the software, in source or
  binary forms, are permitted provided that the following terms and
  conditions are met:

  1) Redistribution of the source code, in verbatim or modified
  form, must retain the above copyright notice, this license,
  the following disclaimer, and any notices that refer to this
  license and/or the following disclaimer.

  2) Redistribution in binary form must include the above copyright
  notice, a copy of this license and the following disclaimer
  in the documentation or with other materials provided with the
  distribution.

  3) Modified copies of the source code must be clearly marked as such,
  and must not be misrepresented as verbatim copies of the source code.

  THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES PROVIDE THE SOFTWARE "AS IS"
  WITHOUT EXPRESSED OR IMPLIED WARRANTY INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE.  IN NO EVENT SHALL ANY COPYRIGHT HOLDER OR OTHER PARTY WHO MAY
  MODIFY AND/OR REDISTRIBUTE THE SOFTWARE UNDER THE TERMS OF THIS LICENSE
  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, LOSS OF DATA OR DATA BECOMING INACCURATE
  OR LOSS OF PROFIT OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF
  THE USE OR INABILITY TO USE THE SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGES.
  */

  /*!
  * Compute the common line intersection among N lines
  *
  * least-square solution, works for any dimension
  */
  void LinesIntersection(const std::vector<Line>& lines, Point& outPoint, float& outFRE);

  /*!
  * compute the distance between a point (p)
  * and a line (a+t*n)
  */
  float PointToLineDistance(const Windows::Foundation::Numerics::float3& point, const Windows::Foundation::Numerics::float3& a, const Windows::Foundation::Numerics::float3& n);
}