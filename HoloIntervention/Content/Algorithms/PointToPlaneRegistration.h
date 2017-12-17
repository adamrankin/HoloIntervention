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

#pragma once

#include "Common.h"
#include "MathCommon.h"

namespace HoloIntervention
{
  namespace Algorithm
  {
    class PointToPlaneRegistration
    {
    public:
      void AddPoint(const Point& point);
      void AddPoint(float x, float y, float z);
      void AddPlane(const Plane& plane);
      void AddPlane(float originX, float originY, float originZ, float normalDirectionI, float normalDirectionJ, float normalDirectionK);
      void AddPlane(const Point& origin, const Vector3& normal);

      Concurrency::task<Windows::Foundation::Numerics::float4x4> ComputeAsync(float& outError);

      void Reset();
      void SetTolerance(float arg);
      float GetTolerance() const;
      uint32 Count() const;

    public:
      PointToPlaneRegistration();
      ~PointToPlaneRegistration();

    protected:
      // Compute the closest point y on a plane from a given point x
      void ClosestPointOnPlane(const cv::Mat& inputPoint, const cv::Mat& origin, const cv::Mat& normal, cv::Mat& outputPoint);

    protected:
      std::vector<Point>      m_points;
      std::vector<Plane>      m_planes;

      float                   m_tolerance = 1e-4f;
    };
  }
}