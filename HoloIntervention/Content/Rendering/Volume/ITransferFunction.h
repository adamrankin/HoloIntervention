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
    struct TransferFunctionLookup
    {
      float MaximumXValue;
      float* LookupTable;
      static const uint32 TRANSFER_FUNCTION_TABLE_SIZE = 1024;

      TransferFunctionLookup()
      {
        LookupTable = new float[TRANSFER_FUNCTION_TABLE_SIZE];
        memset(LookupTable, 0, sizeof(float)*TRANSFER_FUNCTION_TABLE_SIZE);
      }
      ~TransferFunctionLookup()
      {
        delete[] LookupTable;
      }
    };

    class ITransferFunction
    {
    public:
      virtual ~ITransferFunction() {};
      virtual TransferFunctionLookup& GetTFLookupTable() { return m_transferFunction; };
      virtual bool IsValid() const { return m_isValid; }
      virtual void Update() = 0;

    protected:
      ITransferFunction() {};

    protected:
      TransferFunctionLookup m_transferFunction;
      std::atomic_bool m_isValid = false;
    };
  }
}