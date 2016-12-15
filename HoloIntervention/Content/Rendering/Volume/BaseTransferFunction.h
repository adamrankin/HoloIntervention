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

// Local includes
#include "TransferFunctionLookupTable.h"

// STL includes
#include <atomic>
#include <vector>

namespace HoloIntervention
{
  namespace Rendering
  {
    class BaseTransferFunction
    {
      typedef std::pair<uint32, Windows::Foundation::Numerics::float2> ControlPoint;
      typedef std::vector<ControlPoint> ControlPointList;

    public:
      virtual ~BaseTransferFunction();
      virtual TransferFunctionLookupTable& GetTFLookupTable();
      virtual bool IsValid() const;
      virtual void Update() = 0;

      virtual uint32 AddControlPoint(float x, float y);
      virtual uint32 AddControlPoint(const Windows::Foundation::Numerics::float2& point);
      virtual uint32 AddControlPoint(float point[2]);
      virtual uint32 AddControlPoint(const std::array<float, 2>& point);

      virtual bool RemoveControlPoint(uint32 controlPointUid);

    protected:
      BaseTransferFunction();

    protected:
      uint32_t                        m_nextUid = 0;
      ControlPointList                m_controlPoints;
      TransferFunctionLookupTable     m_transferFunction;
      std::atomic_bool                m_isValid = false;
    };
  }
}