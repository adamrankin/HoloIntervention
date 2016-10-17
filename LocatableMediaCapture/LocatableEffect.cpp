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

#include "pch.h"
#include "LocatableEffect.h"

// WinRT includes
#include <wrl\module.h>

using namespace Microsoft::WRL;

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    ActivatableClass(CLocatable);

    /*
    This sample implements a video effect as a Media Foundation transform (MFT).

    NOTES ON THE MFT IMPLEMENTATION

    1. The MFT has fixed streams: One input stream and one output stream.
    2. The MFT supports the following formats: H264
    3. If the MFT is holding an input sample, SetInputType and SetOutputType both fail.
    4. The input and output types must be identical.
    5. If both types are set, no type can be set until the current type is cleared.
    6. Preferred input types:
         (a) If the output type is set, that's the preferred type.
         (b) Otherwise, the preferred types are partial types, constructed from the
             list of supported subtypes.
    7. Preferred output types: As above.
    8. Streaming:
        The private BeingStreaming() method is called in response to the
        MFT_MESSAGE_NOTIFY_BEGIN_STREAMING message.
        If the client does not send MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, the MFT calls
        BeginStreaming inside the first call to ProcessInput or ProcessOutput.
        This is a good approach for allocating resources that your MFT requires for
        streaming.
    9. The configuration attributes are applied in the BeginStreaming method. If the
       client changes the attributes during streaming, the change is ignored until
       streaming is stopped (either by changing the media types or by sending the
       MFT_MESSAGE_NOTIFY_END_STREAMING message) and then restarted.
    */

    // Video FOURCC codes.
    const DWORD FOURCC_H264 = 'H264';

    // Static array of media types (preferred and accepted).
    const GUID g_MediaSubtypes[] =
    {
      MFVideoFormat_H264,
    };

    DWORD GetImageSize(DWORD fcc, UINT32 width, UINT32 height);
    LONG GetDefaultStride(IMFMediaType* pType);

    //----------------------------------------------------------------------------
    template <typename T>
    inline T clamp(const T& val, const T& minVal, const T& maxVal)
    {
      return (val < minVal ? minVal : (val > maxVal ? maxVal : val));
    }

    //----------------------------------------------------------------------------
    CLocatable::CLocatable()
      : m_imageWidthInPixels(0)
      , m_imageHeightInPixels(0)
      , m_imageSize(0)
      , m_streamingInitialized(false)
    {
    }

    //----------------------------------------------------------------------------
    CLocatable::~CLocatable()
    {
    }

    //----------------------------------------------------------------------------
    STDMETHODIMP CLocatable::RuntimeClassInitialize()
    {
      // Create the attribute store.
      return MFCreateAttributes(&m_attributes, 3);
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::SetProperties(ABI::Windows::Foundation::Collections::IPropertySet* pConfiguration)
    {
      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetStreamLimits(DWORD* pdwInputMinimum, DWORD* pdwInputMaximum, DWORD* pdwOutputMinimum, DWORD* pdwOutputMaximum)
    {
      if ((pdwInputMinimum == nullptr) ||
          (pdwInputMaximum == nullptr) ||
          (pdwOutputMinimum == nullptr) ||
          (pdwOutputMaximum == nullptr))
      {
        return E_POINTER;
      }

      // This MFT has a fixed number of streams.
      *pdwInputMinimum = 1;
      *pdwInputMaximum = 1;
      *pdwOutputMinimum = 1;
      *pdwOutputMaximum = 1;
      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetStreamCount(DWORD* pcInputStreams, DWORD* pcOutputStreams)
    {
      if ((pcInputStreams == nullptr) || (pcOutputStreams == nullptr))
      {
        return E_POINTER;
      }

      // This MFT has a fixed number of streams.
      *pcInputStreams = 1;
      *pcOutputStreams = 1;
      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetStreamIDs(DWORD dwInputIDArraySize, DWORD* pdwInputIDs, DWORD dwOutputIDArraySize, DWORD* pdwOutputIDs)
    {
      // It is not required to implement this method if the MFT has a fixed number of
      // streams AND the stream IDs are numbered sequentially from zero (that is, the
      // stream IDs match the stream indexes).

      // In that case, it is OK to return E_NOTIMPL.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetInputStreamInfo(DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO* pStreamInfo)
    {
      if (pStreamInfo == nullptr)
      {
        return E_POINTER;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      if (!IsValidInputStream(dwInputStreamID))
      {
        return MF_E_INVALIDSTREAMNUMBER;
      }

      // NOTE: This method should succeed even when there is no media type on the
      //       stream. If there is no media type, we only need to fill in the dwFlags
      //       member of MFT_INPUT_STREAM_INFO. The other members depend on having a
      //       a valid media type.

      pStreamInfo->hnsMaxLatency = 0;
      pStreamInfo->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES | MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;

      if (m_inputType == nullptr)
      {
        pStreamInfo->cbSize = 0;
      }
      else
      {
        pStreamInfo->cbSize = m_imageSize;
      }

      pStreamInfo->cbMaxLookahead = 0;
      pStreamInfo->cbAlignment = 0;

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetOutputStreamInfo(DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO* pStreamInfo)
    {
      if (pStreamInfo == nullptr)
      {
        return E_POINTER;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      if (!IsValidOutputStream(dwOutputStreamID))
      {
        return MF_E_INVALIDSTREAMNUMBER;
      }

      // NOTE: This method should succeed even when there is no media type on the
      //       stream. If there is no media type, we only need to fill in the dwFlags
      //       member of MFT_OUTPUT_STREAM_INFO. The other members depend on having a
      //       a valid media type.

      pStreamInfo->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES | MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER | MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE;

      if (m_outputType == nullptr)
      {
        pStreamInfo->cbSize = 0;
      }
      else
      {
        pStreamInfo->cbSize = m_imageSize;
      }

      pStreamInfo->cbAlignment = 0;

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetAttributes(IMFAttributes** ppAttributes)
    {
      if (ppAttributes == nullptr)
      {
        return E_POINTER;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      *ppAttributes = m_attributes.Get();
      (*ppAttributes)->AddRef();

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetInputStreamAttributes(DWORD dwInputStreamID, IMFAttributes** ppAttributes)
    {
      // This MFT does not support any stream-level attributes, so the method is not implemented.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetOutputStreamAttributes(DWORD dwOutputStreamID, IMFAttributes** ppAttributes)
    {
      // This MFT does not support any stream-level attributes, so the method is not implemented.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::DeleteInputStream(DWORD dwStreamID)
    {
      // This MFT has a fixed number of input streams, so the method is not supported.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::AddInputStreams(DWORD cStreams, DWORD* adwStreamIDs)
    {
      // This MFT has a fixed number of output streams, so the method is not supported.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetInputAvailableType(DWORD dwInputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType)
    {
      HRESULT hr = S_OK;
      try
      {
        if (ppType == nullptr)
        {
          throw ref new InvalidArgumentException();
        }

        std::lock_guard<std::mutex> guard(m_mutex);

        if (!IsValidInputStream(dwInputStreamID))
        {
          throw ref new Platform::COMException(MF_E_INVALIDSTREAMNUMBER);
        }

        // If the output type is set, return that type as our preferred input type.
        if (m_outputType == nullptr)
        {
          // The output type is not set. Create a partial media type.
          *ppType = OnGetPartialType(dwTypeIndex).Detach();
        }
        else if (dwTypeIndex > 0)
        {
          return MF_E_NO_MORE_TYPES;
        }
        else
        {
          *ppType = m_outputType.Get();
          (*ppType)->AddRef();
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType)
    {
      HRESULT hr = S_OK;

      try
      {
        if (ppType == nullptr)
        {
          throw ref new InvalidArgumentException();
        }

        std::lock_guard<std::mutex> guard(m_mutex);

        if (!IsValidOutputStream(dwOutputStreamID))
        {
          return MF_E_INVALIDSTREAMNUMBER;
        }

        if (m_inputType == nullptr)
        {
          // The input type is not set. Create a partial media type.
          *ppType = OnGetPartialType(dwTypeIndex).Detach();
        }
        else if (dwTypeIndex > 0)
        {
          return MF_E_NO_MORE_TYPES;
        }
        else
        {
          *ppType = m_inputType.Get();
          (*ppType)->AddRef();
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::SetInputType(DWORD dwInputStreamID, IMFMediaType* pType, DWORD dwFlags)
    {
      HRESULT hr = S_OK;

      try
      {
        // Validate flags.
        if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
        {
          throw ref new InvalidArgumentException();
        }

        std::lock_guard<std::mutex> guard(m_mutex);

        if (!IsValidInputStream(dwInputStreamID))
        {
          throw ref new Platform::COMException(MF_E_INVALIDSTREAMNUMBER);
        }

        // Does the caller want us to set the type, or just test it?
        bool reallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

        // If we have an input sample, the client cannot change the type now.
        if (HasPendingOutput())
        {
          throw ref new Platform::COMException(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING);
        }

        // Validate the type, if non-nullptr.
        if (pType != nullptr)
        {
          OnCheckInputType(pType);
        }

        // The type is OK. Set the type, unless the caller was just testing.
        if (reallySet)
        {
          OnSetInputType(pType);
          // When the type changes, end streaming.
          EndStreaming();
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::SetOutputType(DWORD dwOutputStreamID, IMFMediaType* pType, DWORD dwFlags)
    {
      HRESULT hr = S_OK;

      try
      {
        if (!IsValidOutputStream(dwOutputStreamID))
        {
          return MF_E_INVALIDSTREAMNUMBER;
        }

        // Validate flags.
        if (dwFlags & ~MFT_SET_TYPE_TEST_ONLY)
        {
          return E_INVALIDARG;
        }

        std::lock_guard<std::mutex> guard(m_mutex);

        // Does the caller want us to set the type, or just test it?
        bool reallySet = ((dwFlags & MFT_SET_TYPE_TEST_ONLY) == 0);

        // If we have an input sample, the client cannot change the type now.
        if (HasPendingOutput())
        {
          throw ref new Platform::COMException(MF_E_TRANSFORM_CANNOT_CHANGE_MEDIATYPE_WHILE_PROCESSING);
        }

        // Validate the type, if non-nullptr.
        if (pType != nullptr)
        {
          OnCheckOutputType(pType);
        }

        if (reallySet)
        {
          // The type is OK.
          // Set the type, unless the caller was just testing.
          OnSetOutputType(pType);

          EndStreaming();
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetInputCurrentType(DWORD dwInputStreamID, IMFMediaType** ppType)
    {
      if (ppType == nullptr)
      {
        return E_POINTER;
      }

      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      if (!IsValidInputStream(dwInputStreamID))
      {
        hr = MF_E_INVALIDSTREAMNUMBER;
      }
      else if (!m_inputType)
      {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
      }
      else
      {
        *ppType = m_inputType.Get();
        (*ppType)->AddRef();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetOutputCurrentType(DWORD dwOutputStreamID, IMFMediaType** ppType)
    {
      if (ppType == nullptr)
      {
        return E_POINTER;
      }

      HRESULT hr = S_OK;

      std::lock_guard<std::mutex> guard(m_mutex);

      if (!IsValidOutputStream(dwOutputStreamID))
      {
        hr = MF_E_INVALIDSTREAMNUMBER;
      }
      else if (!m_outputType)
      {
        hr = MF_E_TRANSFORM_TYPE_NOT_SET;
      }
      else
      {
        *ppType = m_outputType.Get();
        (*ppType)->AddRef();
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetInputStatus(DWORD dwInputStreamID, DWORD* pdwFlags)
    {
      if (pdwFlags == nullptr)
      {
        return E_POINTER;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      if (!IsValidInputStream(dwInputStreamID))
      {
        return MF_E_INVALIDSTREAMNUMBER;
      }

      // If an input sample is already queued, do not accept another sample until the
      // client calls ProcessOutput or Flush.

      // NOTE: It is possible for an MFT to accept more than one input sample. For
      // example, this might be required in a video decoder if the frames do not
      // arrive in temporal order. In the case, the decoder must hold a queue of
      // samples. For the video effect, each sample is transformed independently, so
      // there is no reason to queue multiple input samples.

      if (m_sample == nullptr)
      {
        *pdwFlags = MFT_INPUT_STATUS_ACCEPT_DATA;
      }
      else
      {
        *pdwFlags = 0;
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::GetOutputStatus(DWORD* pdwFlags)
    {
      if (pdwFlags == nullptr)
      {
        return E_POINTER;
      }

      std::lock_guard<std::mutex> guard(m_mutex);

      // The MFT can produce an output sample if (and only if) there an input sample.
      if (m_sample != nullptr)
      {
        *pdwFlags = MFT_OUTPUT_STATUS_SAMPLE_READY;
      }
      else
      {
        *pdwFlags = 0;
      }

      return S_OK;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::SetOutputBounds(LONGLONG hnsLowerBound, LONGLONG hnsUpperBound)
    {
      // Implementation of this method is optional.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::ProcessEvent(DWORD dwInputStreamID, IMFMediaEvent* pEvent)
    {
      // This MFT does not handle any stream events, so the method can
      // return E_NOTIMPL. This tells the pipeline that it can stop
      // sending any more events to this MFT.
      return E_NOTIMPL;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam)
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      HRESULT hr = S_OK;

      try
      {
        switch (eMessage)
        {
        case MFT_MESSAGE_COMMAND_FLUSH:
          // Flush the MFT.
          OnFlush();
          break;

        case MFT_MESSAGE_COMMAND_DRAIN:
          // Drain: Tells the MFT to reject further input until all pending samples are
          // processed. That is our default behavior already, so there is nothing to do.
          //
          // For a decoder that accepts a queue of samples, the MFT might need to drain
          // the queue in response to this command.
          break;

        case MFT_MESSAGE_SET_D3D_MANAGER:
          // Sets a pointer to the IDirect3DDeviceManager9 interface.

          // The pipeline should never send this message unless the MFT sets the MF_SA_D3D_AWARE
          // attribute set to TRUE. Because this MFT does not set MF_SA_D3D_AWARE, it is an error
          // to send the MFT_MESSAGE_SET_D3D_MANAGER message to the MFT. Return an error code in
          // this case.

          // NOTE: If this MFT were D3D-enabled, it would cache the IMFDXGIDeviceManager
          // pointer for use during streaming.

          throw ref new Platform::COMException(E_NOTIMPL);
          break;

        case MFT_MESSAGE_NOTIFY_BEGIN_STREAMING:
          BeginStreaming();
          break;

        case MFT_MESSAGE_NOTIFY_END_STREAMING:
          EndStreaming();
          break;

        // The next two messages do not require any action from this MFT.

        case MFT_MESSAGE_NOTIFY_END_OF_STREAM:
          break;

        case MFT_MESSAGE_NOTIFY_START_OF_STREAM:
          break;
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::ProcessInput(DWORD dwInputStreamID, IMFSample* pSample, DWORD dwFlags)
    {
      HRESULT hr = S_OK;

      try
      {
        // Check input parameters.
        if (pSample == nullptr)
        {
          throw ref new InvalidArgumentException();
        }

        if (dwFlags != 0)
        {
          throw ref new InvalidArgumentException(); // dwFlags is reserved and must be zero.
        }

        std::lock_guard<std::mutex> guard(m_mutex);

        // Validate the input stream number.
        if (!IsValidInputStream(dwInputStreamID))
        {
          throw ref new Platform::COMException(MF_E_INVALIDSTREAMNUMBER);
        }

        // Check for valid media types.
        // The client must set input and output types before calling ProcessInput.
        if (m_inputType == nullptr || m_outputType == nullptr)
        {
          throw ref new Platform::COMException(MF_E_NOTACCEPTING);
        }

        // Check if an input sample is already queued.
        if (m_sample != nullptr)
        {
          throw ref new Platform::COMException(MF_E_NOTACCEPTING);   // We already have an input sample.
        }

        // Initialize streaming.
        BeginStreaming();

        // Cache the sample. We do the actual work in ProcessOutput.
        m_sample = pSample;
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      return hr;
    }

    //----------------------------------------------------------------------------
    HRESULT CLocatable::ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus)
    {
      HRESULT hr = S_OK;
      std::lock_guard<std::mutex> guard(m_mutex);

      try
      {
        // Check input parameters...

        // This MFT does not accept any flags for the dwFlags parameter.

        // The only defined flag is MFT_PROCESS_OUTPUT_DISCARD_WHEN_NO_BUFFER. This flag
        // applies only when the MFT marks an output stream as lazy or optional. But this
        // MFT has no lazy or optional streams, so the flag is not valid.

        if (dwFlags != 0)
        {
          throw ref new InvalidArgumentException();
        }

        if (pOutputSamples == nullptr || pdwStatus == nullptr)
        {
          throw ref new InvalidArgumentException();
        }

        // There must be exactly one output buffer.
        if (cOutputBufferCount != 1)
        {
          throw ref new InvalidArgumentException();
        }

        // It must contain a sample.
        if (pOutputSamples[0].pSample == nullptr)
        {
          throw ref new InvalidArgumentException();
        }

        ComPtr<IMFMediaBuffer> input;
        ComPtr<IMFMediaBuffer> output;

        // There must be an input sample available for processing.
        if (m_sample == nullptr)
        {
          return MF_E_TRANSFORM_NEED_MORE_INPUT;
        }

        // Initialize streaming.
        BeginStreaming();

        // Get the input buffer.
        ThrowIfError(m_sample->ConvertToContiguousBuffer(&input));

        // Get the output buffer.
        ThrowIfError(pOutputSamples[0].pSample->ConvertToContiguousBuffer(&output));

        OnProcessOutput(input.Get(), output.Get());

        // Set status flags.
        pOutputSamples[0].dwStatus = 0;
        *pdwStatus = 0;

        // Copy the duration and time stamp from the input sample, if present.
        LONGLONG hnsDuration = 0;
        LONGLONG hnsTime = 0;

        if (SUCCEEDED(m_sample->GetSampleDuration(&hnsDuration)))
        {
          ThrowIfError(pOutputSamples[0].pSample->SetSampleDuration(hnsDuration));
        }

        if (SUCCEEDED(m_sample->GetSampleTime(&hnsTime)))
        {
          ThrowIfError(pOutputSamples[0].pSample->SetSampleTime(hnsTime));
        }
      }
      catch (Exception^ exc)
      {
        hr = exc->HResult;
      }

      m_sample.Reset(); // Release our input sample.

      return hr;
    }

    //----------------------------------------------------------------------------
    ComPtr<IMFMediaType> CLocatable::OnGetPartialType(DWORD dwTypeIndex)
    {
      if (dwTypeIndex >= ARRAYSIZE(g_MediaSubtypes))
      {
        throw ref new Platform::COMException(MF_E_NO_MORE_TYPES);
      }

      ComPtr<IMFMediaType> mediaType;
      ThrowIfError(MFCreateMediaType(&mediaType));
      ThrowIfError(mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
      ThrowIfError(mediaType->SetGUID(MF_MT_SUBTYPE, g_MediaSubtypes[dwTypeIndex]));

      return mediaType;
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnCheckInputType(IMFMediaType* pmt)
    {
      assert(pmt != nullptr);

      // If the output type is set, see if they match.
      if (m_outputType != nullptr)
      {
        DWORD flags = 0;
        // IsEqual can return S_FALSE. Treat this as failure.
        if (pmt->IsEqual(m_outputType.Get(), &flags) != S_OK)
        {
          throw ref new Platform::COMException(MF_E_INVALIDMEDIATYPE);
        }
      }
      else
      {
        // Output type is not set. Just check this type.
        OnCheckMediaType(pmt);
      }
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnCheckOutputType(IMFMediaType* pmt)
    {
      assert(pmt != nullptr);

      // If the input type is set, see if they match.
      if (m_inputType != nullptr)
      {
        DWORD flags = 0;
        // IsEqual can return S_FALSE. Treat this as failure.
        if (pmt->IsEqual(m_inputType.Get(), &flags) != S_OK)
        {
          throw ref new Platform::COMException(MF_E_INVALIDMEDIATYPE);
        }
      }
      else
      {
        // Input type is not set. Just check this type.
        OnCheckMediaType(pmt);
      }
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnCheckMediaType(IMFMediaType* pmt)
    {
      bool foundMatchingSubtype = false;

      // Major type must be video.
      GUID major_type;
      ThrowIfError(pmt->GetGUID(MF_MT_MAJOR_TYPE, &major_type));

      if (major_type != MFMediaType_Video)
      {
        throw ref new Platform::COMException(MF_E_INVALIDMEDIATYPE);
      }

      // Subtype must be one of the subtypes in our global list.

      // Get the subtype GUID.
      GUID subtype;
      ThrowIfError(pmt->GetGUID(MF_MT_SUBTYPE, &subtype));

      // Look for the subtype in our list of accepted types.
      for (DWORD i = 0; i < ARRAYSIZE(g_MediaSubtypes); i++)
      {
        if (subtype == g_MediaSubtypes[i])
        {
          foundMatchingSubtype = true;
          break;
        }
      }

      if (!foundMatchingSubtype)
      {
        throw ref new Platform::COMException(MF_E_INVALIDMEDIATYPE); // The MFT does not support this subtype.
      }

      // Reject single-field media types.
      UINT32 interlace = MFGetAttributeUINT32(pmt, MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
      if (interlace == MFVideoInterlace_FieldSingleUpper  || interlace == MFVideoInterlace_FieldSingleLower)
      {
        throw ref new Platform::COMException(MF_E_INVALIDMEDIATYPE);
      }
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnSetInputType(IMFMediaType* pmt)
    {
      // if pmt is nullptr, clear the type.
      // if pmt is non-nullptr, set the type.
      m_inputType = pmt;

      // Update the format information.
      UpdateFormatInfo();
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnSetOutputType(IMFMediaType* pmt)
    {
      // If pmt is nullptr, clear the type. Otherwise, set the type.
      m_outputType = pmt;
    }

    //----------------------------------------------------------------------------
    void CLocatable::BeginStreaming()
    {
      if (!m_streamingInitialized)
      {
        m_streamingInitialized = true;
      }
    }

    //----------------------------------------------------------------------------
    void CLocatable::EndStreaming()
    {
      m_streamingInitialized = false;
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnProcessOutput(IMFMediaBuffer* pIn, IMFMediaBuffer* pOut)
    {
      // Stride if the buffer does not support IMF2DBuffer
      const LONG defaultStride = GetDefaultStride(m_inputType.Get());

      // Set the data size on the output buffer.
      ThrowIfError(pOut->SetCurrentLength(m_imageSize));
    }

    //----------------------------------------------------------------------------
    void CLocatable::OnFlush()
    {
      // For this MFT, flushing just means releasing the input sample.
      m_sample.Reset();
    }

    //----------------------------------------------------------------------------
    void CLocatable::UpdateFormatInfo()
    {
      GUID subtype = GUID_NULL;

      m_imageWidthInPixels = 0;
      m_imageHeightInPixels = 0;

      if (m_inputType != nullptr)
      {
        ThrowIfError(m_inputType->GetGUID(MF_MT_SUBTYPE, &subtype));
        if (subtype == MFVideoFormat_H264)
        {

        }
        else
        {
          throw ref new Platform::COMException(E_UNEXPECTED);
        }

        ThrowIfError(MFGetAttributeSize(m_inputType.Get(), MF_MT_FRAME_SIZE, &m_imageWidthInPixels, &m_imageHeightInPixels));

        // Calculate the image size (not including padding)
        m_imageSize = GetImageSize(subtype.Data1, m_imageWidthInPixels, m_imageHeightInPixels);
      }
    }

    //----------------------------------------------------------------------------
    DWORD GetImageSize(DWORD fcc, UINT32 width, UINT32 height)
    {
      switch (fcc)
      {
      case FOURCC_H264:
        // check overflow
        if ((width > MAXDWORD / 2) || (width * 2 > MAXDWORD / height))
        {
          throw ref new InvalidArgumentException();
        }
        else
        {
          // 16 bpp
          return width * height * 2;
        }
      default:
        // Unsupported type.
        throw ref new Platform::COMException(MF_E_INVALIDTYPE);
      }

      return 0;
    }

    //----------------------------------------------------------------------------
    LONG GetDefaultStride(IMFMediaType* pType)
    {
      LONG stride = 0;

      // Try to get the default stride from the media type.
      if (FAILED(pType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride)))
      {
        // Attribute not set. Try to calculate the default stride.
        GUID subtype = GUID_NULL;

        UINT32 width = 0;
        UINT32 height = 0;

        // Get the subtype and the image size.
        ThrowIfError(pType->GetGUID(MF_MT_SUBTYPE, &subtype));
        ThrowIfError(MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height));
        if (subtype == MFVideoFormat_H264)
        {
          stride = width;
        }
        else
        {
          throw ref new InvalidArgumentException();
        }

        // Set the attribute for later reference.
        (void)pType->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(stride));
      }

      return stride;
    }

    //----------------------------------------------------------------------------
    bool CLocatable::HasPendingOutput() const
    {
      return m_sample != nullptr;
    }

    //----------------------------------------------------------------------------
    bool CLocatable::IsValidInputStream(DWORD dwInputStreamID)
    {
      return dwInputStreamID == 0;
    }

    //----------------------------------------------------------------------------
    bool CLocatable::IsValidOutputStream(DWORD dwOutputStreamID)
    {
      return dwOutputStreamID == 0;
    }
  }
}