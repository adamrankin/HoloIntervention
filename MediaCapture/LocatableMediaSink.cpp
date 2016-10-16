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
#include "LocatableMediaSink.h"
#include "LocatableStreamSink.h"
#include "LocatableMediaSinkProxy.h"

// WinRT includes
#include <InitGuid.h>

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    namespace
    {
      const DWORD c_cbReceiveBuffer = 8 * 1024;

      class ShutdownFunc
      {
      public:
        HRESULT operator()(IMFStreamSink* pStream) const
        {
          static_cast<CStreamSink*>(pStream)->Shutdown();
          return S_OK;
        }
      };

      class StartFunc
      {
      public:
        StartFunc(LONGLONG llStartTime)
          : _llStartTime(llStartTime)
        {
        }

        HRESULT operator()(IMFStreamSink* pStream) const
        {
          return static_cast<CStreamSink*>(pStream)->Start(_llStartTime);
        }

        LONGLONG _llStartTime;
      };

      class StopFunc
      {
      public:
        HRESULT operator()(IMFStreamSink* pStream) const
        {
          return static_cast<CStreamSink*>(pStream)->Stop();
        }
      };

      //----------------------------------------------------------------------------
      template <class T, class TFunc>
      HRESULT ForEach(ComPtrList<T>& col, TFunc fn)
      {
        ComPtrList<T>::POSITION pos = col.FrontPosition();
        ComPtrList<T>::POSITION endPos = col.EndPosition();
        HRESULT hr = S_OK;

        for (; pos != endPos; pos = col.Next(pos))
        {
          ComPtr<T> spStream;

          hr = col.GetItemPos(pos, &spStream);
          if (FAILED(hr))
          {
            break;
          }

          hr = fn(spStream.Get());
        }

        return hr;
      }

      //----------------------------------------------------------------------------
      static void AddAttribute(_In_ GUID guidKey, _In_ IPropertyValue^ value, _In_ IMFAttributes* pAttr)
      {
        PropertyType type = value->Type;
        switch (type)
        {
        case PropertyType::UInt8Array:
        {
          Array<BYTE>^ arr;
          value->GetUInt8Array(&arr);

          ThrowIfError(pAttr->SetBlob(guidKey, arr->Data, arr->Length));
        }
        break;

        case PropertyType::Double:
        {
          ThrowIfError(pAttr->SetDouble(guidKey, value->GetDouble()));
        }
        break;

        case PropertyType::Guid:
        {
          ThrowIfError(pAttr->SetGUID(guidKey, value->GetGuid()));
        }
        break;

        case PropertyType::String:
        {
          ThrowIfError(pAttr->SetString(guidKey, value->GetString()->Data()));
        }
        break;

        case PropertyType::UInt32:
        {
          ThrowIfError(pAttr->SetUINT32(guidKey, value->GetUInt32()));
        }
        break;

        case PropertyType::UInt64:
        {
          ThrowIfError(pAttr->SetUINT64(guidKey, value->GetUInt64()));
        }
        break;
          // ignore unknown values
        }
      }

      //----------------------------------------------------------------------------
      void ConvertPropertiesToMediaType(_In_ IMediaEncodingProperties^ mep, _Outptr_ IMFMediaType** ppMT)
      {
        if (mep == nullptr || ppMT == nullptr)
        {
          throw ref new InvalidArgumentException();
        }
        ComPtr<IMFMediaType> spMT;
        *ppMT = nullptr;
        ThrowIfError(MFCreateMediaType(&spMT));

        auto it = mep->Properties->First();

        while (it->HasCurrent)
        {
          auto currentValue = it->Current;
          AddAttribute(currentValue->Key, safe_cast<IPropertyValue^>(currentValue->Value), spMT.Get());
          it->MoveNext();
        }

        GUID guiMajorType = safe_cast<IPropertyValue^>(mep->Properties->Lookup(MF_MT_MAJOR_TYPE))->GetGuid();

        if (guiMajorType != MFMediaType_Video && guiMajorType != MFMediaType_Audio)
        {
          Throw(E_UNEXPECTED);
        }

        *ppMT = spMT.Detach();
      }

      //----------------------------------------------------------------------------
      DWORD GetStreamId(Windows::Media::Capture::MediaStreamType mediaStreamType)
      {
        switch (mediaStreamType)
        {
        case Windows::Media::Capture::MediaStreamType::VideoRecord:
          return 0;
        case Windows::Media::Capture::MediaStreamType::Audio:
          return 1;
        }

        throw ref new InvalidArgumentException();
      }
    }

    //----------------------------------------------------------------------------
    CMediaSink::CMediaSink()
      : m_referenceCount(1)
      , m_isShutdown(false)
      , m_startTime(0)
    {
    }

    //----------------------------------------------------------------------------
    CMediaSink::~CMediaSink()
    {
      assert(m_isShutdown);
    }

    //----------------------------------------------------------------------------
    HRESULT CMediaSink::RuntimeClassInitialize(ISinkCallback^ callback, IMediaEncodingProperties^ audioEncodingProperties, IMediaEncodingProperties^ videoEncodingProperties)
    {
      try
      {
        m_callback = callback;

        // Set up media streams
        SetMediaStreamProperties(MediaStreamType::Audio, audioEncodingProperties);
        SetMediaStreamProperties(MediaStreamType::VideoRecord, videoEncodingProperties);
      }
      catch (Exception^ exc)
      {
        m_callback = nullptr;
        return exc->HResult;
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    void CMediaSink::SetMediaStreamProperties(Windows::Media::Capture::MediaStreamType MediaStreamType, _In_opt_ IMediaEncodingProperties^ mediaEncodingProperties)
    {
      if (MediaStreamType != MediaStreamType::VideoRecord && MediaStreamType != MediaStreamType::Audio)
      {
        throw ref new InvalidArgumentException();
      }

      RemoveStreamSink(GetStreamId(MediaStreamType));
      const unsigned int streamId = GetStreamId(MediaStreamType);

      if (mediaEncodingProperties != nullptr)
      {
        ComPtr<IMFStreamSink> spStreamSink;
        ComPtr<IMFMediaType> spMediaType;
        ConvertPropertiesToMediaType(mediaEncodingProperties, &spMediaType);
        ThrowIfError(AddStreamSink(streamId, spMediaType.Get(), spStreamSink.GetAddressOf()));
      }
    }

    //----------------------------------------------------------------------------
    HRESULT CMediaSink::CheckShutdown() const
    {
      if (m_isShutdown)
      {
        return MF_E_SHUTDOWN;
      }
      else
      {
        return S_OK;
      }
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::GetCharacteristics(DWORD* pdwCharacteristics)
    {
      if (pdwCharacteristics == NULL)
      {
        return E_INVALIDARG;
      }
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        // Rateless sink.
        *pdwCharacteristics = MEDIASINK_RATELESS;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType* pMediaType, IMFStreamSink** ppStreamSink)
    {
      CStreamSink* pStream = nullptr;
      ComPtr<IMFStreamSink> spMFStream;
      HRESULT hr;
      {
        std::lock_guard<std::mutex> guard(m_mutex);
        hr = CheckShutdown();
      }

      if (SUCCEEDED(hr))
      {
        hr = GetStreamSinkById(dwStreamSinkIdentifier, &spMFStream);
      }

      std::lock_guard<std::mutex> guard(m_mutex);
      if (SUCCEEDED(hr))
      {
        hr = MF_E_STREAMSINK_EXISTS;
      }
      else
      {
        hr = S_OK;
      }

      if (SUCCEEDED(hr))
      {
        pStream = new CStreamSink(dwStreamSinkIdentifier, m_callback);
        if (pStream == nullptr)
        {
          hr = E_OUTOFMEMORY;
        }
        spMFStream.Attach(pStream);
      }

      // Initialize the stream.
      if (SUCCEEDED(hr))
      {
        hr = pStream->Initialize(this);
      }

      if (SUCCEEDED(hr) && pMediaType != nullptr)
      {
        hr = pStream->SetCurrentMediaType(pMediaType);
      }

      if (SUCCEEDED(hr))
      {
        StreamContainer::POSITION pos = m_streams.FrontPosition();
        StreamContainer::POSITION posEnd = m_streams.EndPosition();

        // Insert in proper position
        for (; pos != posEnd; pos = m_streams.Next(pos))
        {
          DWORD dwCurrId;
          ComPtr<IMFStreamSink> spCurr;
          hr = m_streams.GetItemPos(pos, &spCurr);
          if (FAILED(hr))
          {
            break;
          }
          hr = spCurr->GetIdentifier(&dwCurrId);
          if (FAILED(hr))
          {
            break;
          }

          if (dwCurrId > dwStreamSinkIdentifier)
          {
            break;
          }
        }

        if (SUCCEEDED(hr))
        {
          hr = m_streams.InsertPos(pos, pStream);
        }
      }

      if (SUCCEEDED(hr))
      {
        *ppStreamSink = spMFStream.Detach();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::RemoveStreamSink(DWORD dwStreamSinkIdentifier)
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      HRESULT hr = CheckShutdown();
      StreamContainer::POSITION pos = m_streams.FrontPosition();
      StreamContainer::POSITION endPos = m_streams.EndPosition();
      ComPtr<IMFStreamSink> spStream;

      if (SUCCEEDED(hr))
      {
        for (; pos != endPos; pos = m_streams.Next(pos))
        {
          hr = m_streams.GetItemPos(pos, &spStream);
          DWORD dwId;

          if (FAILED(hr))
          {
            break;
          }

          hr = spStream->GetIdentifier(&dwId);
          if (FAILED(hr) || dwId == dwStreamSinkIdentifier)
          {
            break;
          }
        }

        if (pos == endPos)
        {
          hr = MF_E_INVALIDSTREAMNUMBER;
        }
      }

      if (SUCCEEDED(hr))
      {
        hr = m_streams.Remove(pos, nullptr);
        static_cast<CStreamSink*>(spStream.Get())->Shutdown();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::GetStreamSinkCount(_Out_ DWORD* pcStreamSinkCount)
    {
      if (pcStreamSinkCount == NULL)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        *pcStreamSinkCount = m_streams.GetCount();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::GetStreamSinkByIndex(DWORD dwIndex, _Outptr_ IMFStreamSink** ppStreamSink)
    {
      if (ppStreamSink == NULL)
      {
        return E_INVALIDARG;
      }

      ComPtr<IMFStreamSink> spStream;
      std::lock_guard<std::mutex> guard(m_mutex);
      DWORD cStreams = m_streams.GetCount();

      if (dwIndex >= cStreams)
      {
        return MF_E_INVALIDINDEX;
      }

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        StreamContainer::POSITION pos = m_streams.FrontPosition();
        StreamContainer::POSITION endPos = m_streams.EndPosition();
        DWORD dwCurrent = 0;

        for (; pos != endPos && dwCurrent < dwIndex; pos = m_streams.Next(pos), ++dwCurrent)
        {
          // Just move to proper position
        }

        if (pos == endPos)
        {
          hr = MF_E_UNEXPECTED;
        }
        else
        {
          hr = m_streams.GetItemPos(pos, &spStream);
        }
      }

      if (SUCCEEDED(hr))
      {
        *ppStreamSink = spStream.Detach();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink** ppStreamSink)
    {
      if (ppStreamSink == NULL)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);
      HRESULT hr = CheckShutdown();
      ComPtr<IMFStreamSink> spResult;

      if (SUCCEEDED(hr))
      {
        StreamContainer::POSITION pos = m_streams.FrontPosition();
        StreamContainer::POSITION endPos = m_streams.EndPosition();

        for (; pos != endPos; pos = m_streams.Next(pos))
        {
          ComPtr<IMFStreamSink> spStream;
          hr = m_streams.GetItemPos(pos, &spStream);
          DWORD dwId;

          if (FAILED(hr))
          {
            break;
          }

          hr = spStream->GetIdentifier(&dwId);
          if (FAILED(hr))
          {
            break;
          }
          else if (dwId == dwStreamSinkIdentifier)
          {
            spResult = spStream;
            break;
          }
        }

        if (pos == endPos)
        {
          hr = MF_E_INVALIDSTREAMNUMBER;
        }
      }

      if (SUCCEEDED(hr))
      {
        assert(spResult);
        *ppStreamSink = spResult.Detach();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::SetPresentationClock(IMFPresentationClock* pPresentationClock)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      // If we already have a clock, remove ourselves from that clock's
      // state notifications.
      if (SUCCEEDED(hr))
      {
        if (m_presentationClock)
        {
          hr = m_presentationClock->RemoveClockStateSink(this);
        }
      }

      // Register ourselves to get state notifications from the new clock.
      if (SUCCEEDED(hr))
      {
        if (pPresentationClock)
        {
          hr = pPresentationClock->AddClockStateSink(this);
        }
      }

      if (SUCCEEDED(hr))
      {
        // Release the pointer to the old clock.
        // Store the pointer to the new clock.
        m_presentationClock = pPresentationClock;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::GetPresentationClock(IMFPresentationClock** ppPresentationClock)
    {
      if (ppPresentationClock == NULL)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        if (m_presentationClock == NULL)
        {
          hr = MF_E_NO_CLOCK; // There is no presentation clock.
        }
        else
        {
          // Return the pointer to the caller.
          *ppPresentationClock = m_presentationClock.Get();
          (*ppPresentationClock)->AddRef();
        }
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::Shutdown()
    {
      ISinkCallback^ callback;
      {
        std::lock_guard<std::mutex> guard(m_mutex);

        HRESULT hr = CheckShutdown();

        if (SUCCEEDED(hr))
        {
          ForEach(m_streams, ShutdownFunc());
          m_streams.Clear();
          m_presentationClock.Reset();

          m_isShutdown = true;
          callback = m_callback;
        }
      }

      if (callback != nullptr)
      {
        callback->OnShutdown();
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        TRACE(TRACE_LEVEL_LOW, L"OnClockStart ts=%I64d\n", llClockStartOffset);
        // Start each stream.
        m_startTime = llClockStartOffset;
        hr = ForEach(m_streams, StartFunc(llClockStartOffset));
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::OnClockStop(MFTIME hnsSystemTime)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        // Stop each stream
        hr = ForEach(m_streams, StopFunc());
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::OnClockPause(MFTIME hnsSystemTime)
    {
      return MF_E_INVALID_STATE_TRANSITION;
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::OnClockRestart(MFTIME hnsSystemTime)
    {
      return MF_E_INVALID_STATE_TRANSITION;
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CMediaSink::OnClockSetRate(MFTIME hnsSystemTime, float flRate)
    {
      return S_OK;
    }

    //----------------------------------------------------------------------------
    LONGLONG CMediaSink::GetStartTime() const
    {
      return m_startTime;
    }

    //----------------------------------------------------------------------------
    void CMediaSink::HandleError(HRESULT hr)
    {
      Shutdown();
    }
  }
}