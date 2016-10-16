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
#include "LocatableMediaSinkProxy.h"
#include "LocatableDefs.h"
#include "LocatableMediaSink.h"

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    //----------------------------------------------------------------------------
    LocatableMediaSinkProxy::LocatableMediaSinkProxy()
    {
    }

    //----------------------------------------------------------------------------
    LocatableMediaSinkProxy::~LocatableMediaSinkProxy()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      if (m_mediaSink != nullptr)
      {
        m_mediaSink->Shutdown();
        m_mediaSink = nullptr;
      }
    }

    //----------------------------------------------------------------------------
    Windows::Media::IMediaExtension^ LocatableMediaSinkProxy::GetMFExtensions()
    {
      std::lock_guard<std::mutex> guard(m_mutex);

      if (m_mediaSink == nullptr)
      {
        Throw(MF_E_NOT_INITIALIZED);
      }

      ComPtr<IInspectable> spInspectable;
      ThrowIfError(m_mediaSink.As(&spInspectable));

      return safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::IAsyncOperation<IMediaExtension^>^ LocatableMediaSinkProxy::InitializeAsync(IMediaEncodingProperties^ audioEncodingProperties, IMediaEncodingProperties^ videoEncodingProperties)
    {
      return concurrency::create_async([this, videoEncodingProperties, audioEncodingProperties]()
      {
        std::lock_guard<std::mutex> guard(m_mutex);
        CheckShutdown();

        if (m_mediaSink != nullptr)
        {
          Throw(MF_E_ALREADY_INITIALIZED);
        }

        m_callback = ref new LocatableSinkCallback(this);
        if (m_sampleCallbackFunc != nullptr)
        {
          m_callback->RegisterSampleCallback(m_sampleCallbackFunc);
        }

        // Prepare the MF extension
        ThrowIfError(MakeAndInitialize<CMediaSink>(&m_mediaSink, m_callback, audioEncodingProperties, videoEncodingProperties));

        ComPtr<IInspectable> spInspectable;
        ThrowIfError(m_mediaSink.As(&spInspectable));

        return safe_cast<IMediaExtension^>(reinterpret_cast<Object^>(spInspectable.Get()));
      });
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::RegisterSampleCallback(SampleReceivedCallback^ function)
    {
      m_sampleCallbackFunc = function;

      if (m_callback != nullptr)
      {
        m_callback->RegisterSampleCallback(m_sampleCallbackFunc);
      }
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::OnShutdown()
    {
      std::lock_guard<std::mutex> guard(m_mutex);
      if (m_isShutdown)
      {
        return;
      }
      m_isShutdown = true;
      m_mediaSink = nullptr;
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::CheckShutdown()
    {
      if (m_isShutdown)
      {
        Throw(MF_E_SHUTDOWN);
      }
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::LocatableSinkCallback::OnShutdown()
    {
      m_parent->OnShutdown();
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::LocatableSinkCallback::OnSampleReceived(int* sample)
    {
      m_sampleCallback(sample);
    }

    //----------------------------------------------------------------------------
    void LocatableMediaSinkProxy::LocatableSinkCallback::RegisterSampleCallback(SampleReceivedCallback^ sampleCallback)
    {
      m_sampleCallback = sampleCallback;
    }

    //----------------------------------------------------------------------------
    LocatableMediaSinkProxy::LocatableSinkCallback::LocatableSinkCallback(LocatableMediaSinkProxy^ parent)
      : m_parent(parent)
    {

    }
  }
}