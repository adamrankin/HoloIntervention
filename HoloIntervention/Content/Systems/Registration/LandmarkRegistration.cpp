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
#include "LandmarkRegistration.h"

// OpenCV includes
#include <opencv2/core.hpp>

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace System
  {
    namespace
    {
      //----------------------------------------------------------------------------
      inline void perpendiculars(const float x[3], float y[3], float z[3], float theta)
      {
        int dx, dy, dz;

        float x2 = x[0] * x[0];
        float y2 = x[1] * x[1];
        float z2 = x[2] * x[2];
        float r = sqrt(x2 + y2 + z2);

        // transpose the vector to avoid divide-by-zero error
        if (x2 > y2 && x2 > z2)
        {
          dx = 0;
          dy = 1;
          dz = 2;
        }
        else if (y2 > z2)
        {
          dx = 1;
          dy = 2;
          dz = 0;
        }
        else
        {
          dx = 2;
          dy = 0;
          dz = 1;
        }

        float a = x[dx] / r;
        float b = x[dy] / r;
        float c = x[dz] / r;

        float tmp = sqrt(a * a + c * c);

        if (theta != 0)
        {
          float sintheta = sin(theta);
          float costheta = cos(theta);

          if (y)
          {
            y[dx] = (c * costheta - a * b * sintheta) / tmp;
            y[dy] = sintheta * tmp;
            y[dz] = (-a * costheta - b * c * sintheta) / tmp;
          }

          if (z)
          {
            z[dx] = (-c * sintheta - a * b * costheta) / tmp;
            z[dy] = costheta * tmp;
            z[dz] = (a * sintheta - b * c * costheta) / tmp;
          }
        }
        else
        {
          if (y)
          {
            y[dx] = c / tmp;
            y[dy] = 0;
            y[dz] = -a / tmp;
          }

          if (z)
          {
            z[dx] = -a * b / tmp;
            z[dy] = tmp;
            z[dz] = -b * c / tmp;
          }
        }
      }
    }
    //----------------------------------------------------------------------------
    LandmarkRegistration::LandmarkRegistration()
    {
    }

    //----------------------------------------------------------------------------
    LandmarkRegistration::~LandmarkRegistration()
    {
    }

    //----------------------------------------------------------------------------
    void LandmarkRegistration::SetSourceLandmarks(const LandmarkList& landmarks)
    {
      m_sourceLandmarks = landmarks;
    }

    //----------------------------------------------------------------------------
    void LandmarkRegistration::SetSourceLandmarks(const LandmarkListCv& landmarks)
    {
      m_sourceLandmarks.clear();
      for (auto& point : landmarks)
      {
        m_sourceLandmarks.push_back(float3(point.x, point.y, point.z));
      }
    }

    //----------------------------------------------------------------------------
    void LandmarkRegistration::SetTargetLandmarks(const LandmarkList& landmarks)
    {
      m_targetLandmarks = landmarks;
    }

    //----------------------------------------------------------------------------
    void LandmarkRegistration::SetTargetLandmarks(const LandmarkListCv& landmarks)
    {
      m_targetLandmarks.clear();
      for (auto& point : landmarks)
      {
        m_targetLandmarks.push_back(float3(point.x, point.y, point.z));
      }
    }

    //----------------------------------------------------------------------------
    task<float4x4> LandmarkRegistration::CalculateTransformationAsync()
    {
      return create_task([this]() -> float4x4
      {
        float4x4 calibrationMatrix(float4x4::identity());

        if (m_sourceLandmarks.empty() || m_targetLandmarks.empty())
        {
          OutputDebugStringA("Cannot compute registration. Landmark lists are invalid.");
          return float4x4::identity();
        }

        // Derived from vtkLandmarkTransform available at
        //    https://gitlab.kitware.com/vtk/vtk/blob/master/Common/Transforms/vtkLandmarkTransform.cxx

        const uint32_t numberOfPoints = m_sourceLandmarks.size();
        if (numberOfPoints != m_targetLandmarks.size())
        {
          OutputDebugStringA("Cannot compute registration. Landmark lists differ in size.");
          return float4x4::identity();
        }

        // -- find the centroid of each set --
        float3 source_centroid = { 0.f, 0.f, 0.f };
        float3 target_centroid = { 0.f, 0.f, 0.f };
        for (uint32_t i = 0; i < numberOfPoints; i++)
        {
          source_centroid = source_centroid + m_sourceLandmarks[i];
          target_centroid = target_centroid + m_targetLandmarks[i];
        }
        source_centroid /= numberOfPoints * 1.f;
        target_centroid /= numberOfPoints * 1.f;

        // -- if only one point, stop right here
        if (numberOfPoints == 1)
        {
          return make_float4x4_translation(target_centroid.x - source_centroid.x, target_centroid.y - source_centroid.y, target_centroid.z - source_centroid.z);
        }

        // -- build the 3x3 matrix M --
        float M[3][3];
        float AAT[3][3];
        for (uint32_t i = 0; i < 3; i++)
        {
          AAT[i][0] = M[i][0] = 0.0F; // fill M with zeros
          AAT[i][1] = M[i][1] = 0.0F;
          AAT[i][2] = M[i][2] = 0.0F;
        }
        uint32_t pt;
        float3 a, b;
        float sa = 0.0F, sb = 0.0F;
        for (pt = 0; pt < numberOfPoints; pt++)
        {
          // get the origin-centered point (a) in the source set
          a = m_sourceLandmarks[pt] - source_centroid;

          // get the origin-centered point (b) in the target set
          b = m_targetLandmarks[pt] - target_centroid;

          // accumulate the products a*T(b) into the matrix M
          M[0][0] += a.x * b.x;
          M[0][1] += a.x * b.y;
          M[0][2] += a.x * b.z;
          M[1][0] += a.y * b.x;
          M[1][1] += a.y * b.y;
          M[1][2] += a.y * b.z;
          M[2][0] += a.z * b.x;
          M[2][1] += a.z * b.y;
          M[2][2] += a.z * b.z;

          // accumulate scale factors (if desired)
          sa += a.x * a.x + a.y * a.y + a.z * a.z;
          sb += b.x * b.x + b.y * b.y + b.z * b.z;
        }

        // compute required scaling factor (if desired)
        float scale = sqrt(sb / sa);

        // -- build the 4x4 matrix N --
        float Ndata[4][4];
        float* N[4];
        for (uint32_t i = 0; i < 4; i++)
        {
          N[i] = Ndata[i];
          N[i][0] = 0.0F; // fill N with zeros
          N[i][1] = 0.0F;
          N[i][2] = 0.0F;
          N[i][3] = 0.0F;
        }

        // on-diagonal elements
        N[0][0] = M[0][0] + M[1][1] + M[2][2];
        N[1][1] = M[0][0] - M[1][1] - M[2][2];
        N[2][2] = -M[0][0] + M[1][1] - M[2][2];
        N[3][3] = -M[0][0] - M[1][1] + M[2][2];

        // off-diagonal elements
        N[0][1] = N[1][0] = M[1][2] - M[2][1];
        N[0][2] = N[2][0] = M[2][0] - M[0][2];
        N[0][3] = N[3][0] = M[0][1] - M[1][0];

        N[1][2] = N[2][1] = M[0][1] + M[1][0];
        N[1][3] = N[3][1] = M[2][0] + M[0][2];
        N[2][3] = N[3][2] = M[1][2] + M[2][1];

        cv::Mat N_mat(4, 4, CV_32FC1);
        float* mat_data = (float*)N_mat.data;
        for (int i = 0; i < 4; ++i)
        {
          for (int j = 0; j < 4; ++j)
          {
            mat_data[(i * 4) + j] = Ndata[i][j];
          }
        }

        std::vector<float> eigenvalues;
        cv::Mat eigenvectors;
        cv::eigen(N_mat, eigenvalues, eigenvectors);

        // the eigenvector with the largest eigenvalue is the quaternion we want
        float w;
        float3 xProd;

        // first: if points are collinear, choose the quaternion that
        // results in the smallest rotation.
        if (eigenvalues[0] == eigenvalues[1] || numberOfPoints == 2)
        {
          float3 ds = m_sourceLandmarks[1] - m_sourceLandmarks[0];
          float3 dt = m_targetLandmarks[1] - m_targetLandmarks[0];

          float rs = ds.x * ds.x + ds.y * ds.y + ds.z * ds.z;
          float rt = dt.x * dt.x + dt.y * dt.y + dt.z * dt.z;

          // normalize the two vectors
          rs = sqrt(rs);
          ds /= rs;
          rt = sqrt(rt);
          dt /= rt;

          // take dot & cross product
          w = dot(ds, dt);
          xProd = cross(ds, dt);

          float r = length(xProd);
          float theta = atan2(r, w);

          // construct quaternion
          w = cos(theta / 2);
          if (r != 0)
          {
            r = sin(theta / 2) / r;
            xProd *= r;
          }
          else // rotation by 180 degrees: special case
          {
            // rotate around a vector perpendicular to ds
            float ds_arr[3] = { ds.x, ds.y, ds.z };
            float dt_arr[3] = { dt.x, dt.y, dt.z };
            perpendiculars(ds_arr, dt_arr, NULL, 0);
            r = sin(theta / 2);
            xProd = { dt_arr[0]* r, dt_arr[1]* r, dt_arr[2]* r };
          }
        }
        else // points are not collinear
        {
          w = eigenvectors.row(0).at<float>(0);
          xProd = { eigenvectors.row(1).at<float>(0), eigenvectors.row(2).at<float>(0), eigenvectors.row(3).at<float>(0) };
        }

        // convert quaternion to a rotation matrix
        float ww = w* w;
        float wx = w* xProd.x;
        float wy = w* xProd.y;
        float wz = w* xProd.z;

        float xx = xProd.x* xProd.x;
        float yy = xProd.y* xProd.y;
        float zz = xProd.z* xProd.z;

        float xy = xProd.x* xProd.y;
        float xz = xProd.x* xProd.z;
        float yz = xProd.y* xProd.z;

        calibrationMatrix.m11 = ww + xx - yy - zz;
        calibrationMatrix.m21 = 2.f * (wz + xy);
        calibrationMatrix.m31 = 2.f * (-wy + xz);

        calibrationMatrix.m12 = 2.f * (-wz + xy);
        calibrationMatrix.m22 = ww - xx + yy - zz;
        calibrationMatrix.m32 = 2.f * (wx + yz);

        calibrationMatrix.m13 = 2.f * (wy + xz);
        calibrationMatrix.m23 = 2.f * (-wx + yz);
        calibrationMatrix.m33 = ww - xx - yy + zz;

        // compensate for scale factor
        calibrationMatrix = calibrationMatrix* scale;

        // the translation is given by the difference in the transformed source
        // centroid and the target centroid
        float sx, sy, sz;

        sx = calibrationMatrix.m11 * source_centroid.x + calibrationMatrix.m12 * source_centroid.y + calibrationMatrix.m13* source_centroid.z;
        sy = calibrationMatrix.m21 * source_centroid.x + calibrationMatrix.m22 * source_centroid.y + calibrationMatrix.m23* source_centroid.z;
        sz = calibrationMatrix.m31 * source_centroid.x + calibrationMatrix.m32 * source_centroid.y + calibrationMatrix.m33* source_centroid.z;

        calibrationMatrix.m14 = target_centroid.x - sx;
        calibrationMatrix.m24 = target_centroid.y - sy;
        calibrationMatrix.m34 = target_centroid.z - sz;

        return transpose(calibrationMatrix);
      });
    }
  }
}