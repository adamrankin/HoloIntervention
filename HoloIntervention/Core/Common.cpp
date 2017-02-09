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
#include "AppView.h"
#include "Common.h"

// System includes
#include "NotificationSystem.h"

// STL includes
#include <sstream>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Storage;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  bool wait_until_condition(std::function<bool()> func, unsigned int timeoutMs)
  {
    uint32 msCount(0);
    while (!func() && msCount < timeoutMs)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      msCount += 100;
    }

    return msCount < timeoutMs;
  }

  //----------------------------------------------------------------------------
  task<void> complete_after(unsigned int timeoutMs)
  {
    // A task completion event that is set when a timer fires.
    task_completion_event<void> tce;

    // Create a non-repeating timer.
    auto fire_once = new timer<int>(timeoutMs, 0, nullptr, false);

    // Create a call object that sets the completion event after the timer fires.
    auto callback = new call<int>([tce](int)
    {
      tce.set();
    });

    // Connect the timer to the callback and start the timer.
    fire_once->link_target(callback);
    fire_once->start();

    // Create a task that completes after the completion event is set.
    task<void> event_set(tce);

    // Create a continuation task that cleans up resources and
    // and return that continuation task.
    return event_set.then([callback, fire_once]()
    {
      delete callback;
      delete fire_once;
    });
  }

  //----------------------------------------------------------------------------
  void MillimetersToMeters(float4x4& transform)
  {
    transform = transform * make_float4x4_scale(0.001f);
  }

  //----------------------------------------------------------------------------
  std::vector<std::vector<uint32>> NChooseR(uint32 n, uint32 r)
  {
    std::vector<bool> v(n);
    std::fill(v.begin(), v.begin() + r, true);

    std::vector<std::vector<uint32>> output;

    do
    {
      std::vector<uint32> thisCombination;
      for (uint32 i = 0; i < n; ++i)
      {
        if (v[i])
        {
          thisCombination.push_back(i);
        }
      }
      output.push_back(thisCombination);
    }
    while (std::prev_permutation(v.begin(), v.end()));

    return std::move(output);
  }

  //----------------------------------------------------------------------------
  task<void> InitializeTransformRepositoryAsync(UWPOpenIGTLink::TransformRepository^ transformRepository, Platform::String^ fileName)
  {
    return create_task(Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync(fileName)).then([transformRepository](task<StorageFile^> previousTask)
    {
      StorageFile^ file = nullptr;
      try
      {
        file = previousTask.get();
      }
      catch (Platform::Exception^ e)
      {
        throw ref new Platform::Exception(STG_E_FILENOTFOUND, L"Unable to locate system configuration file.");
      }

      XmlDocument^ doc = ref new XmlDocument();
      return create_task(doc->LoadFromFileAsync(file)).then([transformRepository](task<XmlDocument^> previousTask)
      {
        XmlDocument^ doc = nullptr;
        try
        {
          doc = previousTask.get();
        }
        catch (Platform::Exception^ e)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"System configuration file did not contain valid XML.");
        }

        try
        {
          transformRepository->ReadConfiguration(doc);
        }
        catch (Platform::Exception^ e)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"Invalid layout in coordinate definitions configuration area.");
        }
      });
    });
  }

  //----------------------------------------------------------------------------
  Concurrency::task<Windows::Data::Xml::Dom::XmlDocument^> GetXmlDocumentFromFileAsync(Platform::String^ fileName)
  {
    return create_task(Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Data\\configuration.xml")).then([](task<StorageFile^> previousTask)
    {
      StorageFile^ file = nullptr;
      try
      {
        file = previousTask.get();
      }
      catch (Platform::Exception^ e)
      {
        throw ref new Platform::Exception(STG_E_FILENOTFOUND, L"Unable to locate system configuration file.");
      }

      XmlDocument^ doc = ref new XmlDocument();
      return create_task(doc->LoadFromFileAsync(file)).then([](task<XmlDocument^> previousTask) -> XmlDocument^
      {
        XmlDocument^ doc = nullptr;
        try
        {
          doc = previousTask.get();
        }
        catch (Platform::Exception^ e)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"System configuration file did not contain valid XML.");
          return nullptr;
        }

        return doc;
      });
    });
  }

  //----------------------------------------------------------------------------
  std::string ToString(const Windows::Foundation::Numerics::float4x4& matrix)
  {
    std::stringstream ss;
    ss << matrix;
    return ss.str();
  }

  //----------------------------------------------------------------------------
  std::string ToString(const Windows::Foundation::Numerics::float3& vector)
  {
    std::stringstream ss;
    ss << vector;
    return ss.str();
  }

  //----------------------------------------------------------------------------
  Windows::Foundation::Numerics::float3 ExtractNormal(const Windows::Foundation::Numerics::float4x4& matrix)
  {
    // Assumes column order matrix
    // TODO: verify
    return float3(matrix.m31, matrix.m32, matrix.m33);
  }

  //----------------------------------------------------------------------------
  int IsLittleEndian()
  {
    short a = 1;
    return ((char*)&a)[0];
  }
}

//----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float4x4& matrix)
{
  out << matrix.m11 << " " << matrix.m12 << " " << matrix.m13 << " " << matrix.m14 << std::endl
      << matrix.m21 << " " << matrix.m22 << " " << matrix.m23 << " " << matrix.m24 << std::endl
      << matrix.m31 << " " << matrix.m32 << " " << matrix.m33 << " " << matrix.m34 << std::endl
      << matrix.m41 << " " << matrix.m42 << " " << matrix.m43 << " " << matrix.m44;
  return out;
}

//----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float4& vec)
{
  out << vec.x << " " << vec.y << " " << vec.z << " " << vec.w;
  return out;
}

//----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float3& vec)
{
  out << vec.x << " " << vec.y << " " << vec.z;
  return out;
}

//----------------------------------------------------------------------------
std::ostream& operator<<(std::ostream& out, const Windows::Foundation::Numerics::float2& vec)
{
  out << vec.x << " " << vec.y;
  return out;
}