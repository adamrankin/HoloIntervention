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
#include "LandmarkRegistration.h"
#include "PointToLineRegistration.h"

// OpenCV includes
#include <opencv2/core.hpp>

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;

namespace Valhalla
{
  namespace Algorithm
  {
    //----------------------------------------------------------------------------
    PointToLineRegistration::PointToLineRegistration()
    {

    }

    //----------------------------------------------------------------------------
    PointToLineRegistration::PointToLineRegistration(const std::vector<Point>& points, const std::vector<Line>& lines)
      : m_points(points)
      , m_lines(lines)
    {

    }

    //----------------------------------------------------------------------------
    PointToLineRegistration::~PointToLineRegistration()
    {
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::AddPoint(const Point& point)
    {
      m_points.push_back(point);
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::AddPoint(float x, float y, float z)
    {
      m_points.push_back(float3(x, y, z));
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::AddLine(const Line& line)
    {
      m_lines.push_back(line);
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::AddLine(float originX, float originY, float originZ, float directionI, float directionJ, float directionK)
    {
      m_lines.push_back(Line(Point(originX, originY, originZ), Vector3(directionI, directionJ, directionK)));
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::AddLine(const Point& origin, const Vector3& direction)
    {
      m_lines.push_back(Line(origin, direction));
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::Reset()
    {
      m_points.clear();
      m_lines.clear();
    }

    //----------------------------------------------------------------------------
    void PointToLineRegistration::SetTolerance(float arg)
    {
      m_tolerance = arg;
    }

    //----------------------------------------------------------------------------
    float PointToLineRegistration::GetTolerance() const
    {
      return m_tolerance;
    }

    //----------------------------------------------------------------------------
    uint32 PointToLineRegistration::Count() const
    {
      return m_points.size();
    }

    //----------------------------------------------------------------------------
    /*!
    * Point to line registration
    *
    * use ICP to solve the following registration problem:
    *
    * O + a * D = R * X + t
    *
    * INPUTS: X - copied from m_points
    *         O - copied from m_lines.first (origin)
    *         D - copied from m_lines.second (direction)
    *
    * OUTPUTS: 4x4 rotation + translation
    */
    task<float4x4> PointToLineRegistration::ComputeAsync(float& outErrorMm)
    {
      return create_task([this, &outErrorMm]()
      {
        if(m_points.size() != m_lines.size())
        {
          return float4x4::identity() * 0;
        }

        // assume column vector
        int n = m_points.size();
        cv::Mat e = cv::Mat::ones(1, n, CV_32F);
        outErrorMm = std::numeric_limits<float>::infinity();
        cv::Mat E_old = cv::Mat::ones(3, n, CV_32F);
        E_old = E_old * 1000.f;
        cv::Mat E(3, n, CV_32F);

        cv::Mat O = cv::Mat(3, m_points.size(), CV_32F);
        cv::Mat X = cv::Mat(3, m_points.size(), CV_32F);
        cv::Mat Y = cv::Mat(3, m_points.size(), CV_32F);
        cv::Mat D = cv::Mat(3, m_points.size(), CV_32F);

        for(std::vector<Point>::size_type i = 0; i < m_points.size(); ++i)
        {
          X.at<float>(0, i) = m_points[i].x;
          X.at<float>(1, i) = m_points[i].y;
          X.at<float>(2, i) = m_points[i].z;

          O.at<float>(0, i) = m_lines[i].first.x;
          O.at<float>(1, i) = m_lines[i].first.y;
          O.at<float>(2, i) = m_lines[i].first.z;

          D.at<float>(0, i) = m_lines[i].second.x;
          D.at<float>(1, i) = m_lines[i].second.y;
          D.at<float>(2, i) = m_lines[i].second.z;

          Y.at<float>(0, i) = m_lines[i].first.x + m_lines[i].second.x;
          Y.at<float>(1, i) = m_lines[i].first.y + m_lines[i].second.y;
          Y.at<float>(2, i) = m_lines[i].first.z + m_lines[i].second.z;
        }

        cv::Mat d(1, n, CV_32F);
        cv::Mat tempM;
        cv::Mat R = cv::Mat::eye(3, 3, CV_32F);
        cv::Mat t = cv::Mat::zeros(3, 1, CV_32F);

        LandmarkRegistration landmark;
        landmark.SetModeToRigid();

        while(outErrorMm > m_tolerance)
        {
          landmark.SetSourceLandmarks(X);
          landmark.SetTargetLandmarks(Y);
          float4x4 result = landmark.CalculateTransformationAsync().get();
          result = transpose(result);

          Float4x4ToOpenCV(result, R, t);

          tempM = (R * X) + (t * e) - O;

          for(std::vector<Point>::size_type i = 0; i < m_points.size(); ++i)
          {
            d.at<float>(0, i) = tempM.at<float>(0, i) * D.at<float>(0, i) + tempM.at<float>(1, i) * D.at<float>(1, i) + tempM.at<float>(2, i) * D.at<float>(2, i);
          }

          for(int i = 0; i < n; i++)
          {
            for(int j = 0; j < 3; j++)
            {
              Y.at<float>(j, i) = O.at<float>(j, i) + d.at<float>(0, i) * D.at<float>(j, i);
            }
          }

          E = Y - (R * X) - (t * e);
          outErrorMm = static_cast<float>(cv::norm(E - E_old));
          E.copyTo(E_old);
        }

        // compute the Euclidean distance between points and lines
        outErrorMm = 0.0;

        for(int i = 0; i < E.cols; i++)
        {
          outErrorMm += sqrtf(E.at<float>(0, i) * E.at<float>(0, i) + E.at<float>(1, i) * E.at<float>(1, i) + E.at<float>(2, i) * E.at<float>(2, i));
        }

        outErrorMm = outErrorMm * 1000.f / E.cols;

        float4x4 result = float4x4::identity();
        OpenCVToFloat4x4(R, t, result);

        return result;
      });
    }
  }
}