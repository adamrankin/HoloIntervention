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
#include "AsyncCB.h"
#include "LinkList.h"
#include "LocatableDefs.h"
#include "LocatableMediaSinkProxy.h"

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    class CMediaSink;

    class CStreamSink : public IMFStreamSink, public IMFMediaTypeHandler
    {
    public:
      // State enum: Defines the current state of the stream.
      enum State
      {
        State_TypeNotSet = 0,    // No media type is set
        State_Ready,             // Media type is set, Start has never been called.
        State_Started,
        State_Stopped,
        State_Paused,
        State_Count              // Number of states
      };

      // StreamOperation: Defines various operations that can be performed on the stream.
      enum StreamOperation
      {
        OpSetMediaType = 0,
        OpStart,
        OpRestart,
        OpPause,
        OpStop,
        OpProcessSample,
        OpPlaceMarker,

        Op_Count                // Number of operations
      };

      // CAsyncOperation:
      // Used to queue asynchronous operations. When we call MFPutWorkItem, we use this
      // object for the callback state (pState). Then, when the callback is invoked,
      // we can use the object to determine which asynchronous operation to perform.

      class CAsyncOperation : public IUnknown
      {
      public:
        CAsyncOperation(StreamOperation op);

        StreamOperation m_op;   // The operation to perform.

        // IUnknown methods.
        STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
        STDMETHODIMP_(ULONG) AddRef();
        STDMETHODIMP_(ULONG) Release();

      private:
        long    _cRef;
        virtual ~CAsyncOperation();
      };

    public:
      // IUnknown
      IFACEMETHOD(QueryInterface)(REFIID riid, void** ppv);
      IFACEMETHOD_(ULONG, AddRef)();
      IFACEMETHOD_(ULONG, Release)();

      // IMFMediaEventGenerator
      IFACEMETHOD(BeginGetEvent)(IMFAsyncCallback* pCallback, IUnknown* punkState);
      IFACEMETHOD(EndGetEvent)(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent);
      IFACEMETHOD(GetEvent)(DWORD dwFlags, IMFMediaEvent** ppEvent);
      IFACEMETHOD(QueueEvent)(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, PROPVARIANT const* pvValue);

      // IMFStreamSink
      IFACEMETHOD(GetMediaSink)(IMFMediaSink** ppMediaSink);
      IFACEMETHOD(GetIdentifier)(DWORD* pdwIdentifier);
      IFACEMETHOD(GetMediaTypeHandler)(IMFMediaTypeHandler** ppHandler);
      IFACEMETHOD(ProcessSample)(IMFSample* pSample);

      IFACEMETHOD(PlaceMarker)(MFSTREAMSINK_MARKER_TYPE eMarkerType, PROPVARIANT const* pvarMarkerValue, PROPVARIANT const* pvarContextValue);

      IFACEMETHOD(Flush)();

      // IMFMediaTypeHandler
      IFACEMETHOD(IsMediaTypeSupported)(IMFMediaType* pMediaType, IMFMediaType** ppMediaType);
      IFACEMETHOD(GetMediaTypeCount)(DWORD* pdwTypeCount);
      IFACEMETHOD(GetMediaTypeByIndex)(DWORD dwIndex, IMFMediaType** ppType);
      IFACEMETHOD(SetCurrentMediaType)(IMFMediaType* pMediaType);
      IFACEMETHOD(GetCurrentMediaType)(IMFMediaType** ppMediaType);
      IFACEMETHOD(GetMajorType)(GUID* pguidMajorType);

      // ValidStateMatrix: Defines a look-up table that says which operations
      // are valid from which states.
      static BOOL ValidStateMatrix[State_Count][Op_Count];


      CStreamSink(DWORD dwIdentifier, ISinkCallback^ callback);
      virtual ~CStreamSink();

      HRESULT Initialize(CMediaSink* pParent);

      HRESULT CheckShutdown() const;

      HRESULT     Start(MFTIME start);
      HRESULT     Restart();
      HRESULT     Stop();
      HRESULT     Pause();
      HRESULT     Shutdown();
      bool        IsVideo() const;

    private:
      HRESULT     ValidateOperation(StreamOperation op);

      HRESULT     QueueAsyncOperation(StreamOperation op);

      HRESULT     OnDispatchWorkItem(IMFAsyncResult* pAsyncResult);
      bool        ProcessSamplesFromQueue();
      void        HandleError(HRESULT hr);

    private:
      long                        m_referenceCount;           // reference count
      std::mutex                  m_mutex;                    // critical section for thread safety
      ISinkCallback^              m_callback;
      DWORD                       m_identifier;
      State                       m_state;
      bool                        m_isShutdown;               // Flag to indicate if Shutdown() method was called.
      bool                        m_isVideo;
      GUID                        m_guiCurrentSubtype;

      DWORD                       m_workQueueId;              // ID of the work queue for asynchronous operations.
      MFTIME                      m_startTime;                // Presentation time when the clock started.

      ComPtr<IMFMediaSink>        m_sink;                     // Parent media sink
      CMediaSink*                 m_parent;

      ComPtr<IMFMediaEventQueue>  m_eventQueue;               // Event queue
      ComPtr<IMFMediaType>        m_currentType;

      ComPtrList<IUnknown>        m_sampleQueue;              // Queue to hold samples and markers. Applies to: ProcessSample, PlaceMarker
      AsyncCallback<CStreamSink>  m_workQueueCB;              // Callback for the work queue.
      ComPtr<IUnknown>            m_FTM;
    };
  }
}