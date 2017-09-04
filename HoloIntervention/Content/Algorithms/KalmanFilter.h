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

#include <opencv2/video.hpp>

namespace HoloIntervention
{
  namespace Algorithms
  {
    class KalmanFilter
    {
    public:
      const cv::Mat& Predict(const cv::Mat& control = cv::Mat());
      const cv::Mat& Correct(const cv::Mat& measurement);

      void Init(int dynamParams, int measureParams, int controlParams = 0);

      void SetTransitionMatrix(const cv::Mat& transition);
      void SetStatePre(const cv::Mat& statePre);
      void SetStatePre(const std::vector<float>& statePre);

    public:
      KalmanFilter(int dynamParams, int measureParams, int controlParams = 0);
      ~KalmanFilter();

    protected:
      cv::KalmanFilter m_kalmanFilter;
    };
  }
}