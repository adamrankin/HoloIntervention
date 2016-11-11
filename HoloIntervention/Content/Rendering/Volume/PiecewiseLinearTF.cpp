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
    }

    //----------------------------------------------------------------------------
    PiecewiseLinearTF::~PiecewiseLinearTF()
    {
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

      m_isValid = true;
    }
  }
}