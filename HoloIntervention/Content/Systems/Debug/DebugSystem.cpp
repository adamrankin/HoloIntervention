/*====================================================================
Copyright(c) 2017 Adam Rankin


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
#include "DebugSystem.h"
#include "SliceRenderer.h"

// STL includes
#include <sstream>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    DebugSystem::DebugSystem(Rendering::SliceRenderer& sliceRenderer)
      : m_sliceRenderer(sliceRenderer)
    {
    }

    //----------------------------------------------------------------------------
    DebugSystem::~DebugSystem()
    {
    }

    //----------------------------------------------------------------------------
    void DebugSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"show debug"] = [this](SpeechRecognitionResult ^ result)
      {

      };

      callbackMap[L"lock debug"] = [this](SpeechRecognitionResult ^ result)
      {

      };

      callbackMap[L"unlock debug"] = [this](SpeechRecognitionResult ^ result)
      {

        ;
      };
    }

    //----------------------------------------------------------------------------
    void DebugSystem::UpdateValue(const std::wstring& key, const std::wstring& value)
    {
      m_debugValues[key] = value;
    }

    //----------------------------------------------------------------------------
    void DebugSystem::UpdateValue(Platform::String^ key, Platform::String^ value)
    {
      UpdateValue(std::wstring(key->Data()), std::wstring(value->Data()));
    }

    //----------------------------------------------------------------------------
    void DebugSystem::UpdateValue(const std::wstring& key, const float4x4& value)
    {
      std::wstringstream wss;
      wss << value.m11 << L" " << value.m12 << L" " << value.m13 << L" " << value.m14 << L"\n"
          << value.m21 << L" " << value.m22 << L" " << value.m23 << L" " << value.m24 << L"\n"
          << value.m31 << L" " << value.m32 << L" " << value.m33 << L" " << value.m34 << L"\n"
          << value.m41 << L" " << value.m42 << L" " << value.m43 << L" " << value.m44;
      m_debugValues[key] = wss.str();
    }

    //----------------------------------------------------------------------------
    void DebugSystem::UpdateValue(Platform::String^ key, const float4x4& value)
    {
      UpdateValue(std::wstring(key->Data()), value);
    }
  }
}