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
#include "BaseTransferFunction.h"

using namespace DirectX;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    BaseTransferFunction::~BaseTransferFunction()
    {

    }

    //----------------------------------------------------------------------------
    HoloIntervention::Rendering::TransferFunctionLookupTable& BaseTransferFunction::GetTFLookupTable()
    {
      return m_lookupTable;
    }

    //----------------------------------------------------------------------------
    void BaseTransferFunction::SetLookupTableSize(uint32 size)
    {
      m_lookupTable.SetArraySize(size);
    }

    //----------------------------------------------------------------------------
    float BaseTransferFunction::GetMaximumXValue()
    {
      return m_controlPoints.rbegin()->m_inputValue;
    }

    //----------------------------------------------------------------------------
    bool BaseTransferFunction::IsValid() const
    {
      return m_isValid;
    }

    //----------------------------------------------------------------------------
    uint32 BaseTransferFunction::AddControlPoint(float pixelValue, float r, float g, float b)
    {
      return AddControlPoint(pixelValue, r, g, b, 1.f);
    }

    //----------------------------------------------------------------------------
    uint32 BaseTransferFunction::AddControlPoint(float pixelValue, float alphaValue)
    {
      return AddControlPoint(pixelValue, 0.f, 0.f, 0.f, alphaValue);
    }

    //----------------------------------------------------------------------------
    uint32 BaseTransferFunction::AddControlPoint(float pixelValue, float r, float g, float b, float alpha)
    {
      if (pixelValue == 0.f)
      {
        // special case, replace assumed 0,0
        m_controlPoints[0].m_outputValue = XMFLOAT4(r, g, b, alpha);
        return m_controlPoints[0].m_uid;
      }

      for (auto& point : m_controlPoints)
      {
        if (point.m_inputValue == pixelValue)
        {
          throw std::exception("Pixel value control point already exists.");
        }
      }

      m_controlPoints.push_back(ControlPoint(m_nextUid, pixelValue, XMFLOAT4(r, g, b, alpha)));
      std::sort(m_controlPoints.begin(), m_controlPoints.end(), [](auto & left, auto & right)
      {
        return left.m_uid < right.m_uid;
      });

      m_nextUid++;
      m_isValid = false;
      return m_nextUid - 1;
    }

    //----------------------------------------------------------------------------
    bool BaseTransferFunction::RemoveControlPoint(uint32 controlPointUid)
    {
      if (m_controlPoints[0].m_inputValue == controlPointUid)
      {
        // handle special 0 case, reset to assumed 0,0
        m_controlPoints[0].m_outputValue = XMFLOAT4(0.f, 0.f, 0.f, 0.f);
        m_isValid = false;
        return true;
      }

      for (ControlPointList::iterator it = m_controlPoints.begin(); it != m_controlPoints.end(); it++)
      {
        if (it->m_uid == controlPointUid)
        {
          m_controlPoints.erase(it);
          m_isValid = false;
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    BaseTransferFunction::BaseTransferFunction()
    {
      m_controlPoints.push_back(ControlPoint(m_nextUid++, 0.f, XMFLOAT4(0.f, 0.f, 0.f, 0.f)));
    }
  }
}