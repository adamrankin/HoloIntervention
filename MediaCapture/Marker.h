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
#include "LocatableDefs.h"

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    class CMarker : public IMarker
    {
    public:
      static HRESULT Create(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT* pvarMarkerValue, const PROPVARIANT* pvarContextValue, IMarker** ppMarker);

      // IUnknown methods.
      IFACEMETHOD(QueryInterface)(REFIID riid, void** ppv);
      IFACEMETHOD_(ULONG, AddRef)();
      IFACEMETHOD_(ULONG, Release)();

      IFACEMETHOD(GetMarkerType)(MFSTREAMSINK_MARKER_TYPE* pType);
      IFACEMETHOD(GetMarkerValue)(PROPVARIANT* pvar);
      IFACEMETHOD(GetContext)(PROPVARIANT* pvar);

    protected:
      MFSTREAMSINK_MARKER_TYPE _eMarkerType;
      PROPVARIANT _varMarkerValue;
      PROPVARIANT _varContextValue;

    private:
      long    _cRef;

      CMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType);
      virtual ~CMarker();
    };
  }
}