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
#include "LinkList.h"
#include "BaseAttributes.h"
#include "LocatableDefs.h"

using namespace Windows::Media::Capture;
using namespace Windows::Media::MediaProperties;

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    interface class ISinkCallback;
    class CStreamSink;

    class CMediaSink
      : public Microsoft::WRL::RuntimeClass <
        Microsoft::WRL::RuntimeClassFlags< Microsoft::WRL::RuntimeClassType::WinRtClassicComMix >,
        ABI::Windows::Media::IMediaExtension,
        FtmBase,
        IMFMediaSink,
        IMFClockStateSink >
      , public HoloIntervention::LocatableMediaCapture::Common::CBaseAttributes<>
    {
      InspectableClass(L"HoloIntervention.LocatableMediaCapture.LocatableMediaSink", BaseTrust)

    public:
      CMediaSink();
      ~CMediaSink();

      HRESULT RuntimeClassInitialize(ISinkCallback^ callback, IMediaEncodingProperties^ audioEncodingProperties, IMediaEncodingProperties^ videoEncodingProperties);

      // IMediaExtension
      IFACEMETHOD(SetProperties)(ABI::Windows::Foundation::Collections::IPropertySet* pConfiguration)
      {
        return S_OK;
      }

      // IMFMediaSink methods
      IFACEMETHOD(GetCharacteristics)(DWORD* pdwCharacteristics);

      IFACEMETHOD(AddStreamSink)(DWORD dwStreamSinkIdentifier, IMFMediaType* pMediaType, IMFStreamSink** ppStreamSink);

      IFACEMETHOD(RemoveStreamSink)(DWORD dwStreamSinkIdentifier);
      IFACEMETHOD(GetStreamSinkCount)(_Out_ DWORD* pcStreamSinkCount);
      IFACEMETHOD(GetStreamSinkByIndex)(DWORD dwIndex, _Outptr_ IMFStreamSink** ppStreamSink);
      IFACEMETHOD(GetStreamSinkById)(DWORD dwIdentifier, IMFStreamSink** ppStreamSink);
      IFACEMETHOD(SetPresentationClock)(IMFPresentationClock* pPresentationClock);
      IFACEMETHOD(GetPresentationClock)(IMFPresentationClock** ppPresentationClock);
      IFACEMETHOD(Shutdown)();

      // IMFClockStateSink methods
      IFACEMETHOD(OnClockStart)(MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
      IFACEMETHOD(OnClockStop)(MFTIME hnsSystemTime);
      IFACEMETHOD(OnClockPause)(MFTIME hnsSystemTime);
      IFACEMETHOD(OnClockRestart)(MFTIME hnsSystemTime);
      IFACEMETHOD(OnClockSetRate)(MFTIME hnsSystemTime, float flRate);

      LONGLONG GetStartTime() const;

    private:
      typedef ComPtrList<IMFStreamSink> StreamContainer;

    private:
      void HandleError(HRESULT hr);
      void SetMediaStreamProperties(MediaStreamType mediaStreamType, _In_opt_ IMediaEncodingProperties^ mediaEncodingProperties);
      HRESULT CheckShutdown() const;

    private:
      long                                  m_referenceCount;           // reference count
      std::mutex                            m_mutex;                    // critical section for thread safety
      bool                                  m_isShutdown;               // Flag to indicate if Shutdown() method was called.
      LONGLONG                              m_startTime;
      ComPtr<IMFPresentationClock>          m_presentationClock;        // Presentation clock.
      ISinkCallback^                        m_callback;
      StreamContainer                       m_streams;
    };
  }
}