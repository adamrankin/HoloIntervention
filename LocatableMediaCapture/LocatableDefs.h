//// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//// PARTICULAR PURPOSE.
////
//// Copyright (c) Microsoft Corporation. All rights reserved

#pragma once
#ifndef DECLSPEC_UUID
#define DECLSPEC_UUID(x)    __declspec(uuid(x))
#endif

#ifndef DECLSPEC_NOVTABLE
#define DECLSPEC_NOVTABLE   __declspec(novtable)
#endif

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {

interface DECLSPEC_UUID("3AC82233-933C-43a9-AF3D-ADC94EABF406") DECLSPEC_NOVTABLE IMarker :
    public IUnknown
    {
      IFACEMETHOD(GetMarkerType)(MFSTREAMSINK_MARKER_TYPE * pType) = 0;
      IFACEMETHOD(GetMarkerValue)(PROPVARIANT * pvar) = 0;
      IFACEMETHOD(GetContext)(PROPVARIANT * pvar) = 0;
    };

    void FilterOutputMediaType(IMFMediaType* pSourceMediaType, IMFMediaType* pDestinationMediaType);
    void ValidateInputMediaType(REFGUID guidMajorType, REFGUID guidSubtype, IMFMediaType* pMediaType);
    HRESULT CreateMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT* pvarMarkerValue, const PROPVARIANT* pvarContextValue, IMarker** ppMarker);
  }
}