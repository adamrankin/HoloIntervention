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
using namespace Windows::Security::Cryptography;
using namespace Windows::Security::Cryptography::Core;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  bool wait_until_condition(std::function<bool()> func, unsigned int timeoutMs)
  {
    uint32 msCount(0);
    while (!func() && msCount < timeoutMs)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      msCount += 10;
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
  task<void> InitializeTransformRepositoryAsync(Platform::String^ fileName, StorageFolder^ fileStorageFolder, UWPOpenIGTLink::TransformRepository^ transformRepository)
  {
    return create_task(LoadXmlDocumentAsync(fileName, fileStorageFolder)).then([transformRepository](task<XmlDocument^> previousTask)
    {
      XmlDocument^ file = nullptr;
      try
      {
        file = previousTask.get();
        transformRepository->ReadConfiguration(file);
      }
      catch (Platform::Exception^ e)
      {
        throw ref new Platform::Exception(E_INVALIDARG, L"Invalid layout in coordinate definitions configuration area.");
      }
    });
  }

  //----------------------------------------------------------------------------
  Concurrency::task<XmlDocument^> LoadXmlDocumentAsync(Platform::String^ fileName, StorageFolder^ configStorageFolder)
  {
    return create_task(configStorageFolder->GetFileAsync(fileName)).then([](task<StorageFile^> previousTask)
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

  namespace
  {
    //----------------------------------------------------------------------------
    bool icompare_pred(std::wstring::value_type a, std::wstring::value_type b)
    {
      return ::tolower(a) == ::tolower(b);
    }
  }

  //----------------------------------------------------------------------------
  bool IsEqualInsensitive(std::wstring const& a, std::wstring const& b)
  {
    if (a.length() == b.length())
    {
      return std::equal(b.begin(), b.end(), a.begin(), icompare_pred);
    }
    else
    {
      return false;
    }
  }

  //----------------------------------------------------------------------------
  uint64 HashString(const std::wstring& string)
  {
    return HashString(ref new Platform::String(string.c_str()));
  }

  //----------------------------------------------------------------------------
  uint64 HashString(Platform::String^ string)
  {
    auto alg = HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Md5);
    auto buff = CryptographicBuffer::ConvertStringToBinary(string, BinaryStringEncoding::Utf8);
    auto hashed = alg->HashData(buff);
    Platform::Array<byte>^ platArray = ref new Platform::Array<byte>(256);
    CryptographicBuffer::CopyToByteArray(buff, &platArray);
    uint64 result;
    // Grab only first N bytes...
    memcpy(&result, platArray->Data, sizeof(uint64));
    return result;
  }

  //----------------------------------------------------------------------------
  bool HasAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node)
  {
    if (node->Attributes->GetNamedItem(ref new Platform::String(attributeName.c_str())) == nullptr)
    {
      return false;
    }
    return true;
  }

  //----------------------------------------------------------------------------
  bool GetBooleanAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node, bool& outValue)
  {
    auto attribute = node->Attributes->GetNamedItem(ref new Platform::String(attributeName.c_str()));
    if (attribute != nullptr)
    {
      Platform::String^ attributeContent = dynamic_cast<Platform::String^>(attribute->NodeValue);
      if (IsEqualInsensitive(std::wstring(attributeContent->Data()), L"true"))
      {
        outValue = true;
        return true;
      }
      else if (IsEqualInsensitive(std::wstring(attributeContent->Data()), L"false"))
      {
        outValue = false;
        return true;
      }
      return false;
    }

    return false;
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
