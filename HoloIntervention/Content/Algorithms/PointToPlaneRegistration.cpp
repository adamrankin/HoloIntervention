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
#include "LandmarkRegistration.h"
#include "PointToPlaneRegistration.h"

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Algorithm
  {
    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::AddPoint(const Point& point)
    {
      m_points.push_back(point);
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::AddPoint(float x, float y, float z)
    {
      AddPoint(Point(x, y, z));
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::AddPlane(const Plane& plane)
    {
      m_planes.push_back(plane);
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::AddPlane(float originX, float originY, float originZ, float normalDirectionI, float normalDirectionJ, float normalDirectionK)
    {
      m_planes.push_back(Plane(Point(originX, originY, originZ), Vector3(normalDirectionI, normalDirectionJ, normalDirectionK)));
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::AddPlane(const Point& origin, const Vector3& normal)
    {
      m_planes.push_back(Plane(origin, normal));
    }

    //----------------------------------------------------------------------------
    task<float4x4> PointToPlaneRegistration::ComputeAsync(float& outError)
    {
      return create_task([this, &outError]()
      {
        if (m_points.size() != m_planes.size())
        {
          return float4x4::identity() * 0.f;
        }

        cv::Mat origins = cv::Mat(3, m_points.size(), CV_32F);
        cv::Mat points = cv::Mat(3, m_points.size(), CV_32F);
        cv::Mat normals = cv::Mat(3, m_points.size(), CV_32F);
        for (std::vector<Point>::size_type i = 0; i < m_points.size(); ++i)
        {
          points.at<float>(0, i) = m_points[i].x;
          points.at<float>(1, i) = m_points[i].y;
          points.at<float>(2, i) = m_points[i].z;

          origins.at<float>(0, i) = m_planes[i].first.x;
          origins.at<float>(1, i) = m_planes[i].first.y;
          origins.at<float>(2, i) = m_planes[i].first.z;

          normals.at<float>(0, i) = m_planes[i].second.x;
          normals.at<float>(1, i) = m_planes[i].second.y;
          normals.at<float>(2, i) = m_planes[i].second.z;
        }

        LandmarkRegistration landmark;
        landmark.SetModeToRigid();

        // Assume column vectors
        auto n = m_points.size();
        cv::Mat e = cv::Mat::ones(1, n, CV_32F);
        outError = std::numeric_limits<float>::infinity();
        cv::Mat E_old = cv::Mat::ones(3, n, CV_32F);
        E_old = E_old * 1000.f;
        cv::Mat E(3, n, CV_32F);
        cv::Mat Y(3, n, CV_32F);
        cv::Mat A;
        cv::Mat T;

        int iterMax = 2000;

        cv::Mat x(3, 1, CV_32F);
        cv::Mat o(3, 1, CV_32F);
        cv::Mat d(3, 1, CV_32F);
        cv::Mat y(3, 1, CV_32F);
        cv::Mat R = cv::Mat::eye(3, 3, CV_32F);
        cv::Mat t = cv::Mat::zeros(3, 1, CV_32F);

        A = R * points - t * e;

        int iter = 0;
        while (outError > m_tolerance && iter < iterMax)
        {
          iter++;
          // Compute the closest point of X (under current estimation of transformation), on each plane
          for (std::vector<Point>::size_type i = 0; i < n; i++)
          {
            for (int j = 0; j < 3; j++)
            {
              x.at<float>(j, 0) = A.at<float>(j, i);
              o.at<float>(j, 0) = origins.at<float>(j, i);
              d.at<float>(j, 0) = normals.at<float>(j, i);
            }
            ClosestPointOnPlane(x, o, d, y);
            for (int j = 0; j < 3; j++)
            {
              Y.at<float>(j, i) = y.at<float>(j, 0);
            }
          }

          landmark.SetSourceLandmarks(points);
          landmark.SetTargetLandmarks(Y);
          float4x4 result = landmark.CalculateTransformationAsync().get();
          result = transpose(result);

          Float4x4ToOpenCV(result, R, t);

          A = R * points + t * e;
          E = Y - A;

          outError = static_cast<float>(cv::norm(E - E_old));
          E.copyTo(E_old);
        }

        // If the algorithm does not converge within a fixed number of iterations,
        // Set the output transform to invalid (zeros)
        if (iter >= iterMax)
        {
          return float4x4::identity() * 0.f;
        }

        float4x4 result = float4x4::identity();
        OpenCVToFloat4x4(R, t, result);

        return result;
      });
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::Reset()
    {
      m_points.clear();
      m_planes.clear();
      m_tolerance = 1e-4f;
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::SetTolerance(float arg)
    {
      m_tolerance = arg;
    }

    //----------------------------------------------------------------------------
    float PointToPlaneRegistration::GetTolerance() const
    {
      return m_tolerance;
    }

    //----------------------------------------------------------------------------
    uint32 PointToPlaneRegistration::Count() const
    {
      return m_points.size();
    }

    //----------------------------------------------------------------------------
    PointToPlaneRegistration::PointToPlaneRegistration()
    {
    }

    //----------------------------------------------------------------------------
    PointToPlaneRegistration::~PointToPlaneRegistration()
    {
    }

    //----------------------------------------------------------------------------
    void PointToPlaneRegistration::ClosestPointOnPlane(const cv::Mat& inputPoint, const cv::Mat& origin, const cv::Mat& normal, cv::Mat& outputPoint)
    {
      cv::Mat nn(3, 1, CV_32F);
      double l = sqrt(normal.at<float>(0, 0) * normal.at<float>(0, 0) + normal.at<float>(1, 0) * normal.at<float>(1, 0) + normal.at<float>(2, 0) * normal.at<float>(2, 0));
      nn = 1.0 / l * normal;

      outputPoint = inputPoint - (inputPoint - origin).dot(nn) * nn;
    }
  }
}