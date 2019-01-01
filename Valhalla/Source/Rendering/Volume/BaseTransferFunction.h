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

#pragma once

// Local includes
#include "TransferFunctionLookupTable.h"
#include "Rendering\RenderingCommon.h"

// STL includes
#include <atomic>
#include <vector>

namespace Valhalla
{
  namespace Rendering
  {
    class BaseTransferFunction
    {
      struct ControlPoint
      {
        ControlPoint(uint32 uid, float inputValue, DirectX::XMFLOAT4 outputValue)
          : m_uid(uid)
          , m_inputValue(inputValue)
          , m_outputValue(outputValue) {}
        uint32            m_uid;
        float             m_inputValue;
        DirectX::XMFLOAT4 m_outputValue;
      };
      typedef std::vector<ControlPoint> ControlPointList;

    public:
      virtual ~BaseTransferFunction();
      virtual TransferFunctionLookupTable& GetTFLookupTable();
      virtual void SetLookupTableSize(uint32 size);
      virtual float GetMaximumXValue();
      virtual bool IsValid() const;
      virtual void Update() = 0;

      virtual uint32 AddControlPoint(float pixelValue, float r, float g, float b);
      virtual uint32 AddControlPoint(float pixelValue, float alphaValue);

      virtual bool RemoveControlPoint(uint32 controlPointUid);

    protected:
      virtual uint32 AddControlPoint(float pixelValue, float r, float g, float b, float alpha);

      BaseTransferFunction();

    protected:
      uint32_t                        m_nextUid = 0;
      ControlPointList                m_controlPoints;
      TransferFunctionLookupTable     m_lookupTable;
      std::atomic_bool                m_isValid = false;
    };
  }
}