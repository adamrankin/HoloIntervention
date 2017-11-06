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

// Local includes
#include "pch.h"
#include "Debug.h"
#include "SliceEntry.h"
#include "SliceRenderer.h"
#include "TextRenderer.h"

// STL includes
#include <sstream>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  Debug::Debug(Rendering::SliceRenderer& sliceRenderer, const std::shared_ptr<DX::DeviceResources>& deviceResources)
    : m_sliceRenderer(sliceRenderer)
    , m_textRenderer(std::make_unique<Rendering::TextRenderer>(deviceResources, 1920, 1080))
  {
    m_textRenderer->SetFontSize(28);
    m_sliceRenderer.AddSliceAsync(m_textRenderer->GetTexture(), float4x4::identity(), true).then([this](uint64 entryId)
    {
      //m_sliceEntry->SetVisible(false); // off by default
      m_sliceEntry = m_sliceRenderer.GetSlice(entryId);
      m_sliceEntry->SetScalingFactor(0.6f);
    });
  }

  //----------------------------------------------------------------------------
  Debug::~Debug()
  {
  }

  //----------------------------------------------------------------------------
  void Debug::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
  {
    callbackMap[L"show debug"] = [this](SpeechRecognitionResult ^ result)
    {
      m_sliceEntry->SetVisible(true);
    };

    callbackMap[L"hide debug"] = [this](SpeechRecognitionResult ^ result)
    {
      m_sliceEntry->SetVisible(false);
    };

    callbackMap[L"lock debug"] = [this](SpeechRecognitionResult ^ result)
    {
      m_sliceEntry->SetHeadlocked(true);
    };

    callbackMap[L"unlock debug"] = [this](SpeechRecognitionResult ^ result)
    {
      m_sliceEntry->ForceCurrentPose(m_sliceEntry->GetCurrentPose());
      m_sliceEntry->SetHeadlocked(false);
    };
  }

  //----------------------------------------------------------------------------
  void Debug::Update()
  {
    std::wstringstream wss;
    for (auto& pair : m_debugValues)
    {
      wss << pair.first << L": " << pair.second << std::endl;
    }
    m_textRenderer->RenderTextOffscreen(wss.str());
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const std::wstring& value)
  {
    m_debugValues[key] = value;
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, Platform::String^ value)
  {
    UpdateValue(std::wstring(key->Data()), std::wstring(value->Data()));
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const float4x4& value)
  {
    std::wstringstream wss;
    wss << value.m11 << L" " << value.m12 << L" " << value.m13 << L" " << value.m14 << std::endl
        << value.m21 << L" " << value.m22 << L" " << value.m23 << L" " << value.m24 << std::endl
        << value.m31 << L" " << value.m32 << L" " << value.m33 << L" " << value.m34 << std::endl
        << value.m41 << L" " << value.m42 << L" " << value.m43 << L" " << value.m44;
    m_debugValues[key] = wss.str();
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, const float4x4& value)
  {
    UpdateValue(std::wstring(key->Data()), value);
  }
}