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

// STL includes
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// WinRT includes
#include <ppltasks.h>
#include <robuffer.h>
#include <windows.h>

#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)
#define WFILE WIDE1(__FILE__)

#define LOG(level, msg) Log::instance().LogMessage(level, msg, __FILE__, __LINE__)
#define WLOG(level, msg) Log::instance().LogMessage(level, msg, WFILE, __LINE__)

namespace HoloIntervention
{
  static const uint64 INVALID_TOKEN = 0;

  //------------------------------------------------------------------------
  template<class T>
  T clamp(const T& x, const T& upper, const T& lower)
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
    Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> spByteAccess;
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
  template <class T>
  void call_after(const T& callback, unsigned int timeoutMs)
  {
    Concurrency::task_completion_event<void> tce;
    auto call = new Concurrency::call<int>([callback, tce](int)
    {
      callback();
      tce.set();
    });

    auto timer = new Concurrency::timer<int>(timeoutMs, 0, call, false);
    concurrency::task<void> event_set(tce);
    event_set.then([timer, call]()
    {
      delete call;
      delete timer;
    });

    timer->start();
  }

  //-----------------------------------------------------------------------
  bool wait_until_condition(std::function<bool()> func, unsigned int timeoutMs);

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

  std::vector<std::vector<uint32>> NChooseR(uint32 n, uint32 r);
  Concurrency::task<void> InitializeTransformRepositoryAsync(Platform::String^ fileName, Windows::Storage::StorageFolder^ configStorageFolder, UWPOpenIGTLink::TransformRepository^ transformRepository);
  Concurrency::task<Windows::Data::Xml::Dom::XmlDocument^> LoadXmlDocumentAsync(Platform::String^ fileName, Windows::Storage::StorageFolder^ configStorageFolder);

  std::string ToString(const Windows::Foundation::Numerics::float3& vector);
  std::string ToString(const Windows::Foundation::Numerics::float4x4& matrix);
  Windows::Foundation::Numerics::float3 ExtractNormal(const Windows::Foundation::Numerics::float4x4& matrix);

  int IsLittleEndian();

  bool IsEqualInsensitive(std::wstring const& a, std::wstring const& b);
  uint64 HashString(const std::wstring& string);
  uint64 HashString(Platform::String^ string);

  // XML helper functions
  bool HasAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node);
  bool GetBooleanAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node, bool& outValue);
  template<typename T> bool GetScalarAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node, T& outValue);
}

#include "Common.txx"

std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float4x4& matrix);
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float4& vec);
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float3& vec);
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float2& vec);