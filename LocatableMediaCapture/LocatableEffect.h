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

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    class CLocatable WrlSealed : public Microsoft::WRL::RuntimeClass <
      Microsoft::WRL::RuntimeClassFlags< Microsoft::WRL::RuntimeClassType::WinRtClassicComMix >,
      ABI::Windows::Media::IMediaExtension,
      IMFTransform >
    {
      InspectableClass(L"LocatableMediaCapture.LocatableEffect", BaseTrust)

    public:
      CLocatable();
      ~CLocatable();

      STDMETHOD(RuntimeClassInitialize)();

      // IMediaExtension
      STDMETHODIMP SetProperties(ABI::Windows::Foundation::Collections::IPropertySet* pConfiguration);

      // IMFTransform
      STDMETHODIMP GetStreamLimits(DWORD* pdwInputMinimum, DWORD* pdwInputMaximum, DWORD* pdwOutputMinimum, DWORD* pdwOutputMaximum);
      STDMETHODIMP GetStreamCount(DWORD* pcInputStreams, DWORD* pcOutputStreams);
      STDMETHODIMP GetStreamIDs(DWORD dwInputIDArraySize, DWORD* pdwInputIDs, DWORD dwOutputIDArraySize, DWORD* pdwOutputIDs);
      STDMETHODIMP GetInputStreamInfo(DWORD dwInputStreamID, MFT_INPUT_STREAM_INFO* pStreamInfo);
      STDMETHODIMP GetOutputStreamInfo(DWORD dwOutputStreamID, MFT_OUTPUT_STREAM_INFO* pStreamInfo);
      STDMETHODIMP GetAttributes(IMFAttributes** pAttributes);
      STDMETHODIMP GetInputStreamAttributes(DWORD dwInputStreamID, IMFAttributes** ppAttributes);
      STDMETHODIMP GetOutputStreamAttributes(DWORD dwOutputStreamID, IMFAttributes** ppAttributes);
      STDMETHODIMP DeleteInputStream(DWORD dwStreamID);
      STDMETHODIMP AddInputStreams(DWORD cStreams, DWORD* adwStreamIDs);
      STDMETHODIMP GetInputAvailableType(DWORD dwInputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType);
      STDMETHODIMP GetOutputAvailableType(DWORD dwOutputStreamID, DWORD dwTypeIndex, IMFMediaType** ppType);
      STDMETHODIMP SetInputType(DWORD dwInputStreamID, IMFMediaType* pType, DWORD dwFlags);
      STDMETHODIMP SetOutputType(DWORD dwOutputStreamID, IMFMediaType* pType, DWORD dwFlags);
      STDMETHODIMP GetInputCurrentType(DWORD dwInputStreamID, IMFMediaType** ppType);
      STDMETHODIMP GetOutputCurrentType(DWORD dwOutputStreamID, IMFMediaType** ppType);
      STDMETHODIMP GetInputStatus(DWORD dwInputStreamID, DWORD* pdwFlags);
      STDMETHODIMP GetOutputStatus(DWORD* pdwFlags);
      STDMETHODIMP SetOutputBounds(LONGLONG hnsLowerBound, LONGLONG hnsUpperBound);
      STDMETHODIMP ProcessEvent(DWORD dwInputStreamID, IMFMediaEvent* pEvent);
      STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE eMessage, ULONG_PTR ulParam);
      STDMETHODIMP ProcessInput(DWORD dwInputStreamID, IMFSample* pSample, DWORD dwFlags);
      STDMETHODIMP ProcessOutput(DWORD dwFlags, DWORD cOutputBufferCount, MFT_OUTPUT_DATA_BUFFER* pOutputSamples, DWORD* pdwStatus);

    private:
      // HasPendingOutput: Returns TRUE if the MFT is holding an input sample.
      bool HasPendingOutput() const;

      // IsValidInputStream: Returns TRUE if dwInputStreamID is a valid input stream identifier.
      static bool IsValidInputStream(DWORD dwInputStreamID);

      // IsValidOutputStream: Returns TRUE if dwOutputStreamID is a valid output stream identifier.
      static bool IsValidOutputStream(DWORD dwOutputStreamID);

      ComPtr<IMFMediaType> OnGetPartialType(DWORD dwTypeIndex);
      void OnCheckInputType(IMFMediaType* pmt);
      void OnCheckOutputType(IMFMediaType* pmt);
      void OnCheckMediaType(IMFMediaType* pmt);
      void OnSetInputType(IMFMediaType* pmt);
      void OnSetOutputType(IMFMediaType* pmt);
      void BeginStreaming();
      void EndStreaming();
      void OnProcessOutput(IMFMediaBuffer* pIn, IMFMediaBuffer* pOut);
      void OnFlush();
      void UpdateFormatInfo();

      std::mutex m_mutex;

      // Streaming
      bool m_streamingInitialized;
      ComPtr<IMFSample> m_sample;           // Input sample.
      ComPtr<IMFMediaType> m_inputType;     // Input media type.
      ComPtr<IMFMediaType> m_outputType;    // Output media type.

      // Format information
      UINT32 m_imageWidthInPixels;
      UINT32 m_imageHeightInPixels;
      DWORD m_imageSize;                    // Image size, in bytes.

      ComPtr<IMFAttributes> m_attributes;
    };
  }
}