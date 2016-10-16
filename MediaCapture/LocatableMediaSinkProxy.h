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

// stl includes
#include <mutex>
#include <queue>

using namespace Windows::Media::Capture;
using namespace Windows::Media::MediaProperties;

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    class CMediaSink;
    ref class LocatableMediaSinkProxy;

    interface class ISinkCallback
    {
      void OnShutdown();
      virtual void OnSampleReceived(int* sample);
    };

    public delegate void SampleReceivedCallback(int* sample);

    public ref class LocatableMediaSinkProxy sealed
    {
    public:
      LocatableMediaSinkProxy();
      virtual ~LocatableMediaSinkProxy();

      Windows::Media::IMediaExtension^ GetMFExtensions();
      Windows::Foundation::IAsyncOperation<Windows::Media::IMediaExtension^>^ InitializeAsync(IMediaEncodingProperties^ videoEncodingProperties, IMediaEncodingProperties^ audioEncodingProperties);

      void RegisterSampleCallback(SampleReceivedCallback^ function);

    private:
      void OnShutdown();

      ref class LocatableSinkCallback sealed: ISinkCallback
      {
      public:
        virtual void OnShutdown();
        virtual void OnSampleReceived(int* sample);

        void RegisterSampleCallback(SampleReceivedCallback ^ sampleCallback);

      internal:
        LocatableSinkCallback(LocatableMediaSinkProxy ^ parent);

      private:
        LocatableMediaSinkProxy ^      m_parent;
        SampleReceivedCallback ^  m_sampleCallback;
      };

      void CheckShutdown();

    protected private:
      SampleReceivedCallback ^  m_sampleCallbackFunc;
      LocatableSinkCallback ^        m_callback;
      ComPtr<CMediaSink>        m_mediaSink;
      std::mutex                m_mutex;
      bool                      m_isShutdown;
    };
  }
}