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
#include "LocatableStreamSink.h"
#include "LocatableMediaSink.h"

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    EXTERN_GUID(MFSampleExtension_Spatial_CameraCoordinateSystem, 0x9d13c82f, 0x2199, 0x4e67, 0x91, 0xcd, 0xd1, 0xa4, 0x18, 0x1f, 0x25, 0x34);
    EXTERN_GUID(MFSampleExtension_Spatial_CameraViewTransform, 0x4e251fa4, 0x830f, 0x4770, 0x85, 0x9a, 0x4b, 0x8d, 0x99, 0xaa, 0x80, 0x9b);
    EXTERN_GUID(MFSampleExtension_Spatial_CameraProjectionTransform, 0x47f9fcb5, 0x2a02, 0x4f26, 0xa4, 0x77, 0x79, 0x2f, 0xdf, 0x95, 0x88, 0x6a);

#define SET_SAMPLE_FLAG(dest, destMask, pSample, flagName) \
  { \
    UINT32 unValue; \
    if (SUCCEEDED(pSample->GetUINT32(MFSampleExtension_##flagName, &unValue))) \
    { \
      dest |= (unValue != FALSE) ? LocatableSampleFlag_##flagName : 0; \
      destMask |= LocatableSampleFlag_##flagName; \
    } \
  }

#define CHECK_HR(func, message) \
  { \
    HRESULT asi_macro_hr_ = ( func ); \
    if (FAILED(asi_macro_hr_)) \
    { \
      OutputDebugStringA(message); \
    } \
  }

    //----------------------------------------------------------------------------
    CStreamSink::CStreamSink(DWORD dwIdentifier, ISinkCallback^ callback)
      : m_referenceCount(1)
      , m_callback(callback)
      , m_identifier(dwIdentifier)
      , m_state(State_TypeNotSet)
      , m_isShutdown(false)
      , m_isVideo(false)
      , m_startTime(0)
      , m_workQueueId(0)
      , m_parent(nullptr)
#pragma warning(push)
#pragma warning(disable:4355)
      , m_workQueueCB(this, &CStreamSink::OnDispatchWorkItem)
#pragma warning(pop)
    {
      ZeroMemory(&m_guiCurrentSubtype, sizeof(m_guiCurrentSubtype));
    }

    //----------------------------------------------------------------------------
    CStreamSink::~CStreamSink()
    {
      assert(m_isShutdown);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::QueryInterface(REFIID riid, void** ppv)
    {
      if (ppv == nullptr)
      {
        return E_POINTER;
      }
      (*ppv) = nullptr;

      HRESULT hr = S_OK;
      if (riid == IID_IUnknown ||
          riid == IID_IMFStreamSink ||
          riid == IID_IMFMediaEventGenerator)
      {
        (*ppv) = static_cast<IMFStreamSink*>(this);
        AddRef();
      }
      else if (riid == IID_IMFMediaTypeHandler)
      {
        (*ppv) = static_cast<IMFMediaTypeHandler*>(this);
        AddRef();
      }
      else
      {
        hr = E_NOINTERFACE;
      }

      if (FAILED(hr) && riid == IID_IMarshal)
      {
        if (m_FTM == nullptr)
        {
          std::lock_guard<std::mutex> guard(m_mutex);
          if (m_FTM == nullptr)
          {
            hr = CoCreateFreeThreadedMarshaler(static_cast<IMFStreamSink*>(this), &m_FTM);
          }
        }

        if (SUCCEEDED(hr))
        {
          if (m_FTM == nullptr)
          {
            hr = E_UNEXPECTED;
          }
          else
          {
            hr = m_FTM.Get()->QueryInterface(riid, ppv);
          }
        }
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP_(ULONG) CStreamSink::AddRef()
    {
      return InterlockedIncrement(&m_referenceCount);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP_(ULONG) CStreamSink::Release()
    {
      long cRef = InterlockedDecrement(&m_referenceCount);
      if (cRef == 0)
      {
        delete this;
      }
      return cRef;
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
    {
      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        hr = m_eventQueue->BeginGetEvent(pCallback, punkState);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
    {
      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        hr = m_eventQueue->EndGetEvent(pResult, ppEvent);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
    {
      // NOTE:
      // GetEvent can block indefinitely, so we don't hold the lock.
      // This requires some juggling with the event queue pointer.

      HRESULT hr = S_OK;

      ComPtr<IMFMediaEventQueue> spQueue;

      {
        std::lock_guard<std::mutex> guard(m_mutex);

        // Check shutdown
        hr = CheckShutdown();

        // Get the pointer to the event queue.
        if (SUCCEEDED(hr))
        {
          spQueue = m_eventQueue;
        }
      }

      // Now get the event.
      if (SUCCEEDED(hr))
      {
        hr = spQueue->GetEvent(dwFlags, ppEvent);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, PROPVARIANT const* pvValue)
    {
      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        hr = m_eventQueue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetMediaSink(IMFMediaSink** ppMediaSink)
    {
      if (ppMediaSink == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        m_sink.Get()->QueryInterface(IID_IMFMediaSink, (void**)ppMediaSink);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetIdentifier(DWORD* pdwIdentifier)
    {
      if (pdwIdentifier == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        *pdwIdentifier = m_identifier;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetMediaTypeHandler(IMFMediaTypeHandler** ppHandler)
    {
      if (ppHandler == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      // This stream object acts as its own type handler, so we QI ourselves.
      if (SUCCEEDED(hr))
      {
        hr = QueryInterface(IID_IMFMediaTypeHandler, (void**)ppHandler);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::ProcessSample(IMFSample* pSample)
    {
      if (pSample == nullptr)
      {
        return E_INVALIDARG;
      }

      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      hr = CheckShutdown();

      // Validate the operation.
      if (SUCCEEDED(hr))
      {
        hr = ValidateOperation(OpProcessSample);
      }

      if (SUCCEEDED(hr))
      {
        // Add the sample to the sample queue.
        hr = m_sampleQueue.InsertBack(pSample);
      }

      // Unless we are paused, start an async operation to dispatch the next sample.
      if (SUCCEEDED(hr))
      {
        if (m_state != State_Paused)
        {
          // Queue the operation.
          hr = QueueAsyncOperation(OpProcessSample);
        }
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT* pvarMarkerValue, const PROPVARIANT* pvarContextValue)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;
      ComPtr<IMarker> spMarker;

      hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        hr = ValidateOperation(OpPlaceMarker);
      }

      if (SUCCEEDED(hr))
      {
        hr = CreateMarker(eMarkerType, pvarMarkerValue, pvarContextValue, &spMarker);
      }

      if (SUCCEEDED(hr))
      {
        hr = m_sampleQueue.InsertBack(spMarker.Get());
      }

      // Unless we are paused, start an async operation to dispatch the next sample/marker.
      if (SUCCEEDED(hr))
      {
        if (m_state != State_Paused)
        {
          // Queue the operation.
          hr = QueueAsyncOperation(OpPlaceMarker); // Increments ref count on pOp.
        }
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::Flush()
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      HRESULT hr = S_OK;
      try
      {
        ThrowIfError(CheckShutdown());

        m_sampleQueue.Clear();
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::IsMediaTypeSupported(IMFMediaType* pMediaType, IMFMediaType** ppMediaType)
    {
      if (pMediaType == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      GUID majorType = GUID_NULL;
      UINT cbSize = 0;

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
      }

      // First make sure it's video or audio type.
      if (SUCCEEDED(hr))
      {
        if (majorType != MFMediaType_Video && majorType != MFMediaType_Audio)
        {
          hr = MF_E_INVALIDTYPE;
        }
      }

      if (SUCCEEDED(hr) && m_currentType != nullptr)
      {
        //GUID guiNewSubtype;
        //if (FAILED(pMediaType->GetGUID(MF_MT_SUBTYPE, &guiNewSubtype)) ||
        //guiNewSubtype != m_guiCurrentSubtype)
        //{
        //          hr = MF_E_INVALIDTYPE;
        //}
      }

      // We don't return any "close match" types.
      if (ppMediaType)
      {
        *ppMediaType = nullptr;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetMediaTypeCount(DWORD* pdwTypeCount)
    {
      if (pdwTypeCount == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        // We've got only one media type
        *pdwTypeCount = 1;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetMediaTypeByIndex(DWORD dwIndex, IMFMediaType** ppType)
    {
      if (ppType == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (dwIndex > 0)
      {
        hr = MF_E_NO_MORE_TYPES;
      }
      else
      {
        *ppType = m_currentType.Get();
        if (*ppType != nullptr)
        {
          (*ppType)->AddRef();
        }
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::SetCurrentMediaType(IMFMediaType* pMediaType)
    {
      HRESULT hr = S_OK;
      try
      {
        if (pMediaType == nullptr)
        {
          Throw(E_INVALIDARG);
        }

        {
          std::lock_guard<std::mutex> guard(m_mutex);

          ThrowIfError(CheckShutdown());

          // We don't allow format changes after streaming starts.
          ThrowIfError(ValidateOperation(OpSetMediaType));
        }

        // We set media type already
        if (m_state >= State_Ready)
        {
          ThrowIfError(IsMediaTypeSupported(pMediaType, nullptr));
        }

        std::lock_guard<std::mutex> guard(m_mutex);
        GUID guiMajorType;
        pMediaType->GetMajorType(&guiMajorType);
        m_isVideo = (guiMajorType == MFMediaType_Video);

        ThrowIfError(MFCreateMediaType(m_currentType.ReleaseAndGetAddressOf()));
        ThrowIfError(pMediaType->CopyAllItems(m_currentType.Get()));
        ThrowIfError(m_currentType->GetGUID(MF_MT_SUBTYPE, &m_guiCurrentSubtype));
        if (m_state < State_Ready)
        {
          m_state = State_Ready;
        }
        else if (m_state > State_Ready)
        {
          ComPtr<IMFMediaType> spType;
          ThrowIfError(MFCreateMediaType(&spType));
          ThrowIfError(pMediaType->CopyAllItems(spType.Get()));
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }
      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetCurrentMediaType(IMFMediaType** ppMediaType)
    {
      if (ppMediaType == nullptr)
      {
        return E_INVALIDARG;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = CheckShutdown();

      if (SUCCEEDED(hr))
      {
        if (m_currentType == nullptr)
        {
          hr = MF_E_NOT_INITIALIZED;
        }
      }

      if (SUCCEEDED(hr))
      {
        *ppMediaType = m_currentType.Get();
        (*ppMediaType)->AddRef();
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    IFACEMETHODIMP CStreamSink::GetMajorType(GUID* pguidMajorType)
    {
      if (pguidMajorType == nullptr)
      {
        return E_INVALIDARG;
      }

      if (!m_currentType)
      {
        return MF_E_NOT_INITIALIZED;
      }

      *pguidMajorType = (m_isVideo) ? MFMediaType_Video : MFMediaType_Audio;

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::Initialize(CMediaSink* pParent)
    {
      assert(pParent != nullptr);

      HRESULT hr = S_OK;

      // Create the event queue helper.
      hr = MFCreateEventQueue(&m_eventQueue);

      // Allocate a new work queue for async operations.
      if (SUCCEEDED(hr))
      {
        hr = MFAllocateSerialWorkQueue(MFASYNC_CALLBACK_QUEUE_STANDARD, &m_workQueueId);
      }

      if (SUCCEEDED(hr))
      {
        m_sink = pParent;
        m_parent = pParent;
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::CheckShutdown() const
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
    HRESULT CStreamSink::Start(MFTIME start)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;

      hr = ValidateOperation(OpStart);

      if (SUCCEEDED(hr))
      {
        if (start != PRESENTATION_CURRENT_POSITION)
        {
          m_startTime = start;        // Cache the start time.
        }

        m_state = State_Started;
        hr = QueueAsyncOperation(OpStart);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::Stop()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;

      hr = ValidateOperation(OpStop);

      if (SUCCEEDED(hr))
      {
        m_state = State_Stopped;
        hr = QueueAsyncOperation(OpStop);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::Pause()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;

      hr = ValidateOperation(OpPause);

      if (SUCCEEDED(hr))
      {
        m_state = State_Paused;
        hr = QueueAsyncOperation(OpPause);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::Restart()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;

      hr = ValidateOperation(OpRestart);

      if (SUCCEEDED(hr))
      {
        m_state = State_Started;
        hr = QueueAsyncOperation(OpRestart);
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    // Class-static matrix of operations vs states.
    // If an entry is TRUE, the operation is valid from that state.
    BOOL CStreamSink::ValidStateMatrix[CStreamSink::State_Count][CStreamSink::Op_Count] =
    {
      // States:    Operations:
      //            SetType   Start     Restart   Pause     Stop      Sample    Marker
      /* NotSet */  TRUE,     FALSE,    FALSE,    FALSE,    FALSE,    FALSE,    FALSE,

      /* Ready */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     FALSE,    TRUE,

      /* Start */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     TRUE,     TRUE,

      /* Pause */   TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,

      /* Stop */    TRUE,     TRUE,     FALSE,    FALSE,    TRUE,     FALSE,    TRUE,

    };

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::ValidateOperation(StreamOperation op)
    {
      assert(!m_isShutdown);

      HRESULT hr = S_OK;

      if (ValidStateMatrix[m_state][op])
      {
        return S_OK;
      }
      else if (m_state == State_TypeNotSet)
      {
        return MF_E_NOT_INITIALIZED;
      }
      else
      {
        return MF_E_INVALIDREQUEST;
      }
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::Shutdown()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      if (!m_isShutdown)
      {
        if (m_eventQueue)
        {
          m_eventQueue->Shutdown();
        }

        MFUnlockWorkQueue(m_workQueueId);

        m_sampleQueue.Clear();

        m_sink.Reset();
        m_eventQueue.Reset();
        m_currentType.Reset();

        m_isShutdown = true;
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    bool CStreamSink::IsVideo() const
    {
      return m_isVideo;
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::QueueAsyncOperation(StreamOperation op)
    {
      HRESULT hr = S_OK;
      ComPtr<CAsyncOperation> spOp;
      spOp.Attach(new CAsyncOperation(op)); // Created with ref count = 1
      if (!spOp)
      {
        hr = E_OUTOFMEMORY;
      }

      if (SUCCEEDED(hr))
      {
        hr = MFPutWorkItem2(m_workQueueId, 0, &m_workQueueCB, spOp.Get());
      }

      TRACEHR_RET(hr);
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::OnDispatchWorkItem(IMFAsyncResult* pAsyncResult)
    {
      // Called by work queue thread. Need to hold the critical section.
      try
      {
        StreamOperation op(Op_Count);
        {
          std::lock_guard<std::mutex> guard(m_mutex);
          ComPtr<IUnknown> spState;
          ThrowIfError(pAsyncResult->GetState(&spState));

          // The state object is a CAsncOperation object.
          CAsyncOperation* pOp = static_cast<CAsyncOperation*>(spState.Get());
          op = pOp->m_op;
        }

        switch (op)
        {
        case OpStart:
        case OpRestart:
          // Send MEStreamSinkStarted.
          ThrowIfError(QueueEvent(MEStreamSinkStarted, GUID_NULL, S_OK, nullptr));
          ThrowIfError(QueueEvent(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr));
          break;
        case OpStop:
        {
          {
            std::lock_guard<std::mutex> guard(m_mutex);
            // Drop samples from queue.
            m_sampleQueue.Clear();
          }

          // Send the event even if the previous call failed.
          ThrowIfError(QueueEvent(MEStreamSinkStopped, GUID_NULL, S_OK, nullptr));
          break;
        }
        case OpPause:
        {
          ThrowIfError(QueueEvent(MEStreamSinkPaused, GUID_NULL, S_OK, nullptr));
          break;
        }
        case OpProcessSample:
        case OpPlaceMarker:
        case OpSetMediaType:
        {
          if (ProcessSamplesFromQueue())
          {
            if (op == OpProcessSample)
            {
              ThrowIfError(QueueEvent(MEStreamSinkRequestSample, GUID_NULL, S_OK, nullptr));
            }
          }
        }
        break;
        }
      }
      catch (Exception^ exc)
      {
        HandleError(exc->HResult);
      }
      return S_OK;
    }

    //----------------------------------------------------------------------------
    bool CStreamSink::ProcessSamplesFromQueue()
    {
      bool needMoreSamples = false;

      ComPtr<IUnknown> unknownSample;

      bool sendSamples = true;
      bool sendEOS = false;

      if (FAILED(m_sampleQueue.RemoveFront(&unknownSample)))
      {
        needMoreSamples = true;
        sendSamples = false;
      }

      while (sendSamples)
      {
        ComPtr<IMFSample> sample;
        bool processingSample = false;
        assert(unknownSample);

        // Figure out if this is a marker or a sample.
        if (SUCCEEDED(unknownSample.As(&sample)))
        {
          assert(sample);    // Not a marker, must be a sample

          ComPtr<IUnknown> spUnknown;
          Windows::Perception::Spatial::SpatialCoordinateSystem^ spSpatialCoordinateSystem;
          Windows::Foundation::Numerics::float4x4 worldToCameraMatrix;
          Windows::Foundation::Numerics::float4x4 viewMatrix;
          Windows::Foundation::Numerics::float4x4 projectionMatrix;
          UINT32 cbBlobSize = 0;
          auto hr = sample->GetUnknown(MFSampleExtension_Spatial_CameraCoordinateSystem, IID_PPV_ARGS(&spUnknown));
          if (SUCCEEDED(hr))
          {
            spSpatialCoordinateSystem = reinterpret_cast<Windows::Perception::Spatial::SpatialCoordinateSystem^>(spUnknown.Get());
            //hr = spUnknown.As(&spSpatialCoordinateSystem);
            //if (FAILED(hr))
            //{
            //            return hr;
            //}
            hr = sample->GetBlob(MFSampleExtension_Spatial_CameraViewTransform,
                                 (UINT8*)(&viewMatrix),
                                 sizeof(viewMatrix),
                                 &cbBlobSize);
            if (SUCCEEDED(hr) && cbBlobSize == sizeof(Windows::Foundation::Numerics::float4x4))
            {
              hr = sample->GetBlob(MFSampleExtension_Spatial_CameraProjectionTransform,
                                   (UINT8*)(&projectionMatrix),
                                   sizeof(projectionMatrix),
                                   &cbBlobSize);
              if (SUCCEEDED(hr) && cbBlobSize == sizeof(Windows::Foundation::Numerics::float4x4))
              {
                //XMMATRIX transform;
                //auto tryTransform = m_coordSystem->TryGetTransformTo(reinterpret_cast<Windows::Perception::Spatial::SpatialCoordinateSystem^>(spSpatialCoordinateSystem.Get()));
                //if (tryTransform != nullptr)
                //{
                //                transform = XMLoadFloat4x4(&tryTransform->Value);
                //}
              }
            }

            m_callback->OnSampleReceived(nullptr);
          }
        }
        else
        {
          ComPtr<IMarker> marker;
          // Check if it is a marker
          if (SUCCEEDED(unknownSample.As(&marker)))
          {
            MFSTREAMSINK_MARKER_TYPE markerType;
            PROPVARIANT var;
            PropVariantInit(&var);
            ThrowIfError(marker->GetMarkerType(&markerType));
            // Get the context data.
            ThrowIfError(marker->GetContext(&var));

            HRESULT hr = QueueEvent(MEStreamSinkMarker, GUID_NULL, S_OK, &var);

            PropVariantClear(&var);

            ThrowIfError(hr);

            if (markerType == MFSTREAMSINK_MARKER_ENDOFSEGMENT)
            {
              sendEOS = true;
            }
          }
          else
          {
            ComPtr<IMFMediaType> type;
            ThrowIfError(unknownSample.As(&type));
            // TODO : anyone need to know the media type was changed?
          }
        }

        if (sendSamples)
        {
          if (FAILED(m_sampleQueue.RemoveFront(unknownSample.ReleaseAndGetAddressOf())))
          {
            needMoreSamples = true;
            sendSamples = false;
          }
        }

      }

      if (sendEOS)
      {
        ComPtr<CMediaSink> parent = m_parent;
        concurrency::create_task([parent]()
        {
          //parent->ReportEndOfStream();
        });
      }

      return needMoreSamples;
    }

    //----------------------------------------------------------------------------
    CStreamSink::CAsyncOperation::CAsyncOperation(StreamOperation op)
      : _cRef(1)
      , m_op(op)
    {
    }

    //----------------------------------------------------------------------------
    CStreamSink::CAsyncOperation::~CAsyncOperation()
    {
      assert(_cRef == 0);
    }

    //----------------------------------------------------------------------------
    ULONG CStreamSink::CAsyncOperation::AddRef()
    {
      return InterlockedIncrement(&_cRef);
    }

    //----------------------------------------------------------------------------
    ULONG CStreamSink::CAsyncOperation::Release()
    {
      ULONG cRef = InterlockedDecrement(&_cRef);
      if (cRef == 0)
      {
        delete this;
      }

      return cRef;
    }

    //----------------------------------------------------------------------------
    HRESULT CStreamSink::CAsyncOperation::QueryInterface(REFIID iid, void** ppv)
    {
      if (!ppv)
      {
        return E_POINTER;
      }
      if (iid == IID_IUnknown)
      {
        *ppv = static_cast<IUnknown*>(this);
      }
      else
      {
        *ppv = nullptr;
        return E_NOINTERFACE;
      }
      AddRef();
      return S_OK;
    }

    //----------------------------------------------------------------------------
    void CStreamSink::HandleError(HRESULT hr)
    {
      if (!m_isShutdown)
      {
        QueueEvent(MEError, GUID_NULL, hr, nullptr);
      }
    }
  }
}