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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class SliceEntry;
    class SliceRenderer;
    class TextRenderer;
  }

  class Debug : public Input::IVoiceInput, public IEngineComponent
  {
  public:
    Debug(Rendering::SliceRenderer& sliceRenderer, const std::shared_ptr<DX::DeviceResources>& deviceResources);
    ~Debug();

  public:
    // IVoiceInput functions
    virtual void RegisterVoiceCallbacks(HoloIntervention::Input::VoiceInputCallbackMap& callbackMap);

  public:
    void Update();

    void UpdateValue(const std::wstring& key, const std::wstring& value);
    void UpdateValue(const std::wstring& key, const Windows::Foundation::Numerics::float2& value);
    void UpdateValue(const std::wstring& key, const Windows::Foundation::Numerics::float3& value);
    void UpdateValue(const std::wstring& key, const Windows::Foundation::Numerics::float4& value);
    void UpdateValue(const std::wstring& key, const Windows::Foundation::Numerics::float4x4& value);
    void UpdateValue(Platform::String^ key, Platform::String^ value);
    void UpdateValue(Platform::String^ key, const Windows::Foundation::Numerics::float2& value);
    void UpdateValue(Platform::String^ key, const Windows::Foundation::Numerics::float3& value);
    void UpdateValue(Platform::String^ key, const Windows::Foundation::Numerics::float4& value);
    void UpdateValue(Platform::String^ key, const Windows::Foundation::Numerics::float4x4& value);

  protected:
    // Cached variables
    Rendering::SliceRenderer& m_sliceRenderer;

    // Text rendering variables
    std::unique_ptr<Rendering::TextRenderer>  m_textRenderer;

    // Class variables
    std::map<std::wstring, std::wstring>      m_debugValues;
    std::atomic_bool                          m_worldLocked = false;
    std::shared_ptr<Rendering::SliceEntry>    m_sliceEntry = nullptr;
  };
}