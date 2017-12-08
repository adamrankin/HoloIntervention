/*====================================================================
Copyright(c) 2017 Adam Rankin


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
#include "MathCommon.h"

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  bool OpenCVToFloat4x4(const cv::InputArray& inMatrix, float4x4& outMatrix)
  {
    if (inMatrix.cols() != 4 || inMatrix.rows() != 4 || inMatrix.channels() != 1 || inMatrix.depth() != CV_32F)
    {
      return false;
    }

    auto mat = inMatrix.getMat();
    outMatrix.m11 = mat.at<float>(0, 0);
    outMatrix.m12 = mat.at<float>(0, 1);
    outMatrix.m13 = mat.at<float>(0, 2);
    outMatrix.m14 = mat.at<float>(0, 3);

    outMatrix.m21 = mat.at<float>(1, 0);
    outMatrix.m22 = mat.at<float>(1, 1);
    outMatrix.m23 = mat.at<float>(1, 2);
    outMatrix.m24 = mat.at<float>(1, 3);

    outMatrix.m31 = mat.at<float>(2, 0);
    outMatrix.m32 = mat.at<float>(2, 1);
    outMatrix.m33 = mat.at<float>(2, 2);
    outMatrix.m34 = mat.at<float>(2, 3);

    outMatrix.m41 = mat.at<float>(3, 0);
    outMatrix.m42 = mat.at<float>(3, 1);
    outMatrix.m43 = mat.at<float>(3, 2);
    outMatrix.m44 = mat.at<float>(3, 3);

    return true;
  }

  //----------------------------------------------------------------------------
  bool Float4x4ToOpenCV(const float4x4& inMatrix, cv::Mat& outMatrix)
  {
    float array[16];
    if (!Float4x4ToArray(inMatrix, array))
    {
      return false;
    }

    outMatrix = cv::Mat(4, 4, CV_32F, array);

    return true;
  }

  //----------------------------------------------------------------------------
  bool Float4x4ToArray(const float4x4& inMatrix, float outMatrix[16])
  {
    outMatrix[0] = inMatrix.m11;
    outMatrix[1] = inMatrix.m12;
    outMatrix[2] = inMatrix.m13;
    outMatrix[3] = inMatrix.m14;

    outMatrix[4] = inMatrix.m21;
    outMatrix[5] = inMatrix.m22;
    outMatrix[6] = inMatrix.m23;
    outMatrix[7] = inMatrix.m24;

    outMatrix[8] = inMatrix.m31;
    outMatrix[9] = inMatrix.m32;
    outMatrix[10] = inMatrix.m33;
    outMatrix[11] = inMatrix.m34;

    outMatrix[12] = inMatrix.m41;
    outMatrix[13] = inMatrix.m42;
    outMatrix[14] = inMatrix.m43;
    outMatrix[15] = inMatrix.m44;

    return true;
  }

  //----------------------------------------------------------------------------
  bool Float4x4ToArray(const float4x4& inMatrix, std::array<float, 16> outMatrix)
  {
    return Float4x4ToArray(inMatrix, outMatrix.data());
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const float (&inMatrix)[16], float4x4& outMatrix)
  {
    outMatrix.m11 = inMatrix[0];
    outMatrix.m12 = inMatrix[1];
    outMatrix.m13 = inMatrix[2];
    outMatrix.m14 = inMatrix[3];

    outMatrix.m21 = inMatrix[4];
    outMatrix.m22 = inMatrix[5];
    outMatrix.m23 = inMatrix[6];
    outMatrix.m24 = inMatrix[7];

    outMatrix.m31 = inMatrix[8];
    outMatrix.m32 = inMatrix[9];
    outMatrix.m33 = inMatrix[10];
    outMatrix.m34 = inMatrix[11];

    outMatrix.m41 = inMatrix[12];
    outMatrix.m42 = inMatrix[13];
    outMatrix.m43 = inMatrix[14];
    outMatrix.m44 = inMatrix[15];

    return true;
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const std::array<float, 16>& inMatrix, float4x4& outMatrix)
  {
    return ArrayToFloat4x4(inMatrix.data(), 16, outMatrix);
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const float (&inMatrix)[9], float4x4& outMatrix)
  {
    outMatrix = float4x4::identity();

    outMatrix.m11 = inMatrix[0];
    outMatrix.m12 = inMatrix[1];
    outMatrix.m13 = inMatrix[2];

    outMatrix.m21 = inMatrix[4];
    outMatrix.m22 = inMatrix[5];
    outMatrix.m23 = inMatrix[6];

    outMatrix.m31 = inMatrix[8];
    outMatrix.m32 = inMatrix[9];
    outMatrix.m33 = inMatrix[10];

    return true;
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const std::array<float, 9>& inMatrix, float4x4& outMatrix)
  {
    return ArrayToFloat4x4(inMatrix.data(), 9, outMatrix);
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const float* inMatrix, uint32 matrixSize, Windows::Foundation::Numerics::float4x4& outMatrix)
  {
    if (matrixSize != 3 && matrixSize != 4)
    {
      return false;
    }

    outMatrix = float4x4::identity();

    if (matrixSize == 3)
    {
      outMatrix.m11 = inMatrix[0];
      outMatrix.m12 = inMatrix[1];
      outMatrix.m13 = inMatrix[2];

      outMatrix.m21 = inMatrix[3];
      outMatrix.m22 = inMatrix[4];
      outMatrix.m23 = inMatrix[5];

      outMatrix.m31 = inMatrix[6];
      outMatrix.m32 = inMatrix[7];
      outMatrix.m33 = inMatrix[8];
    }
    else
    {
      outMatrix.m11 = inMatrix[0];
      outMatrix.m12 = inMatrix[1];
      outMatrix.m13 = inMatrix[2];
      outMatrix.m14 = inMatrix[3];

      outMatrix.m21 = inMatrix[4];
      outMatrix.m22 = inMatrix[5];
      outMatrix.m23 = inMatrix[6];
      outMatrix.m24 = inMatrix[7];

      outMatrix.m31 = inMatrix[8];
      outMatrix.m32 = inMatrix[9];
      outMatrix.m33 = inMatrix[10];
      outMatrix.m34 = inMatrix[11];

      outMatrix.m41 = inMatrix[12];
      outMatrix.m42 = inMatrix[13];
      outMatrix.m43 = inMatrix[14];
      outMatrix.m44 = inMatrix[15];
    }

    return true;
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const float(&inMatrix)[4][4], Windows::Foundation::Numerics::float4x4& outMatrix)
  {
    outMatrix.m11 = inMatrix[0][0];
    outMatrix.m12 = inMatrix[0][1];
    outMatrix.m13 = inMatrix[0][2];
    outMatrix.m14 = inMatrix[0][3];

    outMatrix.m21 = inMatrix[1][0];
    outMatrix.m22 = inMatrix[1][1];
    outMatrix.m23 = inMatrix[1][2];
    outMatrix.m24 = inMatrix[1][3];

    outMatrix.m31 = inMatrix[2][0];
    outMatrix.m32 = inMatrix[2][1];
    outMatrix.m33 = inMatrix[2][2];
    outMatrix.m34 = inMatrix[2][3];

    outMatrix.m41 = inMatrix[3][0];
    outMatrix.m42 = inMatrix[3][1];
    outMatrix.m43 = inMatrix[3][2];
    outMatrix.m44 = inMatrix[3][3];

    return true;
  }

  //----------------------------------------------------------------------------
  bool ArrayToFloat4x4(const float(&inMatrix)[3][3], Windows::Foundation::Numerics::float4x4& outMatrix)
  {
    outMatrix.m11 = inMatrix[0][0];
    outMatrix.m12 = inMatrix[0][1];
    outMatrix.m13 = inMatrix[0][2];

    outMatrix.m21 = inMatrix[1][0];
    outMatrix.m22 = inMatrix[1][1];
    outMatrix.m23 = inMatrix[1][2];

    outMatrix.m31 = inMatrix[2][0];
    outMatrix.m32 = inMatrix[2][1];
    outMatrix.m33 = inMatrix[2][2];

    return true;
  }

  //----------------------------------------------------------------------------
  std::wstring PrintMatrix(const Windows::Foundation::Numerics::float4x4& matrix)
  {
    std::wostringstream woss;
    woss << matrix.m11 << " "
         << matrix.m12 << " "
         << matrix.m13 << " "
         << matrix.m14 << "    "
         << matrix.m21 << " "
         << matrix.m22 << " "
         << matrix.m23 << " "
         << matrix.m24 << "    "
         << matrix.m31 << " "
         << matrix.m32 << " "
         << matrix.m33 << " "
         << matrix.m34 << "    "
         << matrix.m41 << " "
         << matrix.m42 << " "
         << matrix.m43 << " "
         << matrix.m44 << std::endl;
    return woss.str();
  }

  //----------------------------------------------------------------------------
  void LinesIntersection(const std::vector<Line>& lines, Point& outPoint, float& outFRE)
  {
    /*
    based on the following doc by Johannes Traa (UIUC 2013)

    Least-Squares Intersection of Lines
    http://cal.cs.illinois.edu/~johannes/research/LS_line_intersect.pdf

    */
    auto sizex = 3;

    outPoint = float3::zero();

    cv::Mat I = cv::Mat::eye(sizex, sizex, CV_32F);
    cv::Mat R = cv::Mat::zeros(sizex, sizex, CV_32F);
    cv::Mat q = cv::Mat::zeros(sizex, 1, CV_32F);

    cv::Mat lineDirectionNormalized = cv::Mat::zeros(sizex, 1, CV_32F);
    cv::Mat lineOrigin = cv::Mat::zeros(sizex, 1, CV_32F);
    cv::Mat tempR, tempq;

    for (std::vector<float3>::size_type i = 0; i < lines.size(); i++)
    {
      // normalize N
      float3 norm = normalize(lines[i].second);

      lineDirectionNormalized.at<float>(0, 0) = norm.x;
      lineDirectionNormalized.at<float>(1, 0) = norm.y;
      lineDirectionNormalized.at<float>(2, 0) = norm.z;
      lineOrigin.at<float>(0, 0) = lines[i].first.x;
      lineOrigin.at<float>(1, 0) = lines[i].first.y;
      lineOrigin.at<float>(2, 0) = lines[i].first.z;

      cv::Mat tempLineDirectionNormTranspose;
      cv::transpose(lineDirectionNormalized, tempLineDirectionNormTranspose);
      tempR = I - lineDirectionNormalized * tempLineDirectionNormTranspose;
      tempq = tempR * lineOrigin;

      R = R + tempR;
      q = q + tempq;
    }

    cv::Mat RInverse;
    cv::invert(R, RInverse);
    cv::Mat output = RInverse * q;

    outPoint.x = output.at<float>(0, 0);
    outPoint.y = output.at<float>(1, 0);
    outPoint.z = output.at<float>(2, 0);

    // compute FRE
    outFRE = 0.0;
    for (std::vector<float3>::size_type i = 0; i < lines.size(); i++)
    {
      float3 norm = normalize(lines[i].second);
      outFRE += PointToLineDistance(outPoint, lines[i].first, norm);
    }
    outFRE = outFRE / lines.size();
  }

  //----------------------------------------------------------------------------
  float PointToLineDistance(const float3& point, const float3& lineOrigin, const float3& lineDirection)
  {
    auto point2 = point + (10 * lineDirection);
    return length(cross(point - lineOrigin, point - point2)) / length(point2 - lineOrigin);
  }
}