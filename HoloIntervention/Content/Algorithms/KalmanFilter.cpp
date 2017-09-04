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
#include "Common.h"
#include "KalmanFilter.h"

using namespace cv;

namespace HoloIntervention
{
  namespace Algorithms
  {
    //----------------------------------------------------------------------------
    KalmanFilter::KalmanFilter(int dynamParams, int measureParams, int controlParams)
    {
      Init(dynamParams, measureParams, controlParams);
    }

    //----------------------------------------------------------------------------
    KalmanFilter::~KalmanFilter()
    {
    }

    //----------------------------------------------------------------------------
    const Mat& KalmanFilter::Predict(const Mat& control /*= Mat()*/)
    {
      return m_kalmanFilter.predict(control);
    }

    //----------------------------------------------------------------------------
    const Mat& KalmanFilter::Correct(const Mat& measurement)
    {
      return m_kalmanFilter.correct(measurement);
    }

    //----------------------------------------------------------------------------
    void KalmanFilter::Init(int dynamParams, int measureParams, int controlParams)
    {
      m_kalmanFilter.init(dynamParams, measureParams, controlParams);
    }

    //----------------------------------------------------------------------------
    void KalmanFilter::SetTransitionMatrix(const Mat& transition)
    {
      m_kalmanFilter.transitionMatrix = transition;
    }

    //----------------------------------------------------------------------------
    void KalmanFilter::SetStatePre(const Mat& statePre)
    {
      m_kalmanFilter.statePre = statePre;
    }

    //----------------------------------------------------------------------------
    void KalmanFilter::SetStatePre(const std::vector<float>& statePre)
    {
      Mat state(statePre);
      m_kalmanFilter.statePre = state;
    }
  }
}