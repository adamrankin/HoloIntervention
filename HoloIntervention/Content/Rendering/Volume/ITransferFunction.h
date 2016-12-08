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

#pragma once

// STL includes
#include <atomic>

namespace HoloIntervention
{
  namespace Rendering
  {
    class TransferFunctionLookup
    {
    public:
      TransferFunctionLookup()
      {
        m_lookupTable = new float[m_transferFunctionTableSize];
        memset(m_lookupTable, 0, sizeof(float)*m_transferFunctionTableSize);
      }
      ~TransferFunctionLookup()
      {
        delete[] m_lookupTable;
      }

      float GetMaximumXValue() { return m_maximumXValue; }
      void SetMaximumXValue(float maxXValue) { m_maximumXValue = maxXValue; }
      float* GetLookupTableArray() { return m_lookupTable; }
      uint32 GetArraySize() { return m_transferFunctionTableSize; }

    protected:
      float                 m_maximumXValue;
      float*                m_lookupTable;
      uint32                m_transferFunctionTableSize = 256;
    };

    class ITransferFunction
    {
      typedef std::pair<uint32, Windows::Foundation::Numerics::float2> ControlPoint;
      typedef std::vector<ControlPoint> ControlPointList;

    public:
      virtual ~ITransferFunction() {};
      virtual TransferFunctionLookup& GetTFLookupTable()
      {
        return m_transferFunction;
      };
      virtual bool IsValid() const
      {
        return m_isValid;
      }
      virtual void Update() = 0;

      virtual uint32 AddControlPoint(float x, float y)
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

        m_controlPoints.push_back(ControlPoint(m_nextUid, Windows::Foundation::Numerics::float2(x, y)));
        std::sort(m_controlPoints.begin(), m_controlPoints.end(), [](auto & left, auto & right)
        {
          return left.first < right.first;
        });

        m_nextUid++;
        m_isValid = false;
        return m_nextUid - 1;
      }

      virtual uint32 AddControlPoint(const Windows::Foundation::Numerics::float2& point)
      {
        return AddControlPoint(point.x, point.y);
      }

      virtual uint32 AddControlPoint(float point[2])
      {
        return AddControlPoint(point[0], point[1]);
      }

      virtual uint32 AddControlPoint(const std::array<float, 2>& point)
      {
        return AddControlPoint(point[0], point[1]);
      }

      virtual bool RemoveControlPoint(uint32 controlPointUid)
      {
        if (m_controlPoints[0].first == controlPointUid)
        {
          // handle special 0 case, reset to assumed 0,0
          m_controlPoints[0].second.y = 0.f;
          m_isValid = false;
          return true;
        }

        for (ControlPointList::iterator it = m_controlPoints.begin(); it != m_controlPoints.end(); it++)
        {
          if (it->first == controlPointUid)
          {
            m_controlPoints.erase(it);
            m_isValid = false;
            return true;
          }
        }

        return false;
      }

    protected:
      ITransferFunction()
      {
        m_controlPoints.push_back(ControlPoint(m_nextUid++, Windows::Foundation::Numerics::float2(0.f, 0.f)));
      };

    protected:
      uint32_t                  m_nextUid = 0;
      ControlPointList          m_controlPoints;
      TransferFunctionLookup    m_transferFunction;
      std::atomic_bool          m_isValid = false;
    };
  }
}