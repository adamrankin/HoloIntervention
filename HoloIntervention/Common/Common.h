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

// std includes
#include <string>

// WinRT includes
#include <ppltasks.h>
#include <robuffer.h>
#include <windows.h>

namespace HoloIntervention
{
  //------------------------------------------------------------------------
  template<class T>
  const T& clamp(const T& x, const T& upper, const T& lower)
  {
    return min(upper, max(x, lower));
  }

  //------------------------------------------------------------------------
  template <typename t = byte>
  t * GetDataFromIBuffer(Windows::Storage::Streams::IBuffer^ container)
  {
    if (container == nullptr)
    {
      return nullptr;
    }

    unsigned int bufferLength = container->Length;

    if (!(bufferLength > 0))
    {
      return nullptr;
    }

    HRESULT hr = S_OK;

    Microsoft::WRL::ComPtr<IUnknown> pUnknown = reinterpret_cast<IUnknown*>(container);
    Microsoft::WRL::ComPtr<IBufferByteAccess> spByteAccess;
    hr = pUnknown.As(&spByteAccess);
    if (FAILED(hr))
    {
      return nullptr;
    }

    byte* pRawData = nullptr;
    hr = spByteAccess->Buffer(&pRawData);
    if (FAILED(hr))
    {
      return nullptr;
    }

    return reinterpret_cast<t*>(pRawData);
  }

  //------------------------------------------------------------------------
  template<typename Functor>
  void RunFunctionAfterDelay(uint32 delayMs, Functor function)
  {
    // Convert ms to 100-nanosecond
    int64 delay100ns = delayMs * 10000;

    TimeSpan ts;
    ts.Duration = 10000000;
    TimerElapsedHandler^ handler = ref new TimerElapsedHandler(function);
    ThreadPoolTimer^ timer = ThreadPoolTimer::CreateTimer(handler, ts);
  }

  //------------------------------------------------------------------------
  Concurrency::task<void> complete_after(unsigned int timeoutMs);

  //------------------------------------------------------------------------
  template<typename T>
  Concurrency::task<T> cancel_after_timeout(Concurrency::task<T> t, Concurrency::cancellation_token_source cts, unsigned int timeoutMs)
  {
    // Create a task that returns true after the specified task completes.
    Concurrency::task<bool> success_task = t.then([](T)
    {
      return true;
    });
    // Create a task that returns false after the specified timeout.
    Concurrency::task<bool> failure_task = complete_after(timeoutMs).then([]
    {
      return false;
    });

    // Create a continuation task that cancels the overall task
    // if the timeout task finishes first.
    return (failure_task || success_task).then([t, cts](bool success)
    {
      if (!success)
      {
        // Set the cancellation token. The task that is passed as the
        // t parameter should respond to the cancellation and stop
        // as soon as it can.
        cts.cancel();
      }

      // Return the original task.
      return t;
    });
  }

  //----------------------------------------------------------------------------
  void MillimetersToMeters(Windows::Foundation::Numerics::float4x4& transform);

  //----------------------------------------------------------------------------
  int IsLittleEndian();
}