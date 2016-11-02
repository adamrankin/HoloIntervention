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
#include "PiecewiseLinearTF.h"

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    PiecewiseLinearTF::PiecewiseLinearTF()
    {
      m_controlPoints.push_back(ControlPoint(m_nextUid++, float2(0.f, 0.f)));
    }

    //----------------------------------------------------------------------------
    PiecewiseLinearTF::~PiecewiseLinearTF()
    {
    }

    //----------------------------------------------------------------------------
    uint32 PiecewiseLinearTF::AddControlPoint(float x, float y)
    {
      if (x == 0.f)
      {
        // special case, replace assumed 0,0
        m_controlPoints[0].second.y = y;
        return m_controlPoints[0].first;
      }

      for (auto& point : m_controlPoints)
      {
        if (point.second.x == x)
        {
          throw new std::exception("X value control point already exists.");
        }
      }

      m_controlPoints.push_back(ControlPoint(m_nextUid, float2(x, y)));
      std::sort(m_controlPoints.begin(), m_controlPoints.end(), [](auto & left, auto & right)
      {
        return left.first < right.first;
      });

      m_nextUid++;
      return m_nextUid - 1;
    }

    //----------------------------------------------------------------------------
    uint32 PiecewiseLinearTF::AddControlPoint(const float2& point)
    {
      return AddControlPoint(point.x, point.y);
    }

    //----------------------------------------------------------------------------
    uint32 PiecewiseLinearTF::AddControlPoint(float point[2])
    {
      return AddControlPoint(point[0], point[1]);
    }

    //----------------------------------------------------------------------------
    uint32 PiecewiseLinearTF::AddControlPoint(const std::array<float, 2>& point)
    {
      return AddControlPoint(point[0], point[1]);
    }

    //----------------------------------------------------------------------------
    bool PiecewiseLinearTF::RemoveControlPoint(uint32 controlPointUid)
    {
      if (m_controlPoints[0].first == controlPointUid)
      {
        // handle special 0 case, reset to assumed 0,0
        m_controlPoints[0].second.y = 0.f;
        return true;
      }

      for (ControlPointList::iterator it = m_controlPoints.begin(); it != m_controlPoints.end(); it++)
      {
        if (it->first == controlPointUid)
        {
          m_controlPoints.erase(it);
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    void PiecewiseLinearTF::Update()
    {
      // given a set of control points, divide up the function range into TRANSFER_FUNCTION_TABLE_SIZE equally spaced entries
      // function range is from 0 to max value provided by control points
      if (m_controlPoints.size() < 2)
      {
        throw new std::exception("Not enough control points to compute a function. Need at least 2.");
      }

      m_transferFunction.MaximumXValue = m_controlPoints.rbegin()->second.x;

      for (unsigned int i = 0; i < TransferFunctionLookup::TRANSFER_FUNCTION_TABLE_SIZE; ++i)
      {
        auto ratio = (1.f * i) / TransferFunctionLookup::TRANSFER_FUNCTION_TABLE_SIZE - 1;
        auto xValue = ratio * m_transferFunction.MaximumXValue;
        // find xValue in control points
        for (unsigned int j = 1; j < m_controlPoints.size(); ++j)
        {
          if (xValue >= m_controlPoints[j - 1].second.x && xValue < m_controlPoints[j].second.x)
          {
            // found it, linear interpolate y value and store
            auto thisXRange = m_controlPoints[j].second.x - m_controlPoints[j - 1].second.x;
            auto thisYRange = m_controlPoints[j].second.y - m_controlPoints[j - 1].second.y;
            auto offset = xValue - m_controlPoints[j - 1].second.x;
            m_transferFunction.LookupTable[i] = m_controlPoints[j - 1].second.y + (offset / thisXRange) * thisYRange;
          }
        }
      }
    }
  }
}