/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "ModelRenderer.h"
#include "Slice.h"
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
    : m_textRenderer(std::make_unique<Rendering::TextRenderer>(deviceResources, 1920, 1080))
  {
    m_textRenderer->SetFontSize(28);
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
      m_debugShowing = true;
      m_sliceEntry->SetVisible(m_debugShowing);

      std::lock_guard<std::mutex> guard(m_coordinateSystemModelLock);
      for (auto& entry : m_coordinateSystemModels)
      {
        entry.second.second->SetVisible(true);
      }
    };

    callbackMap[L"hide debug"] = [this](SpeechRecognitionResult ^ result)
    {
      m_debugShowing = false;
      m_sliceEntry->SetVisible(m_debugShowing);

      std::lock_guard<std::mutex> guard(m_coordinateSystemModelLock);
      for (auto& entry : m_coordinateSystemModels)
      {
        entry.second.second->SetVisible(false);
      }
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
  void Debug::Update(SpatialCoordinateSystem^ hmdCoordinateSystem)
  {
    if (m_sliceEntry == nullptr || !m_sliceEntry->GetVisible())
    {
      return;
    }

    std::wstringstream wss;
    {
      std::lock_guard<std::mutex> guard(m_debugLock);
      for (auto& pair : m_debugValues)
      {
        wss << pair.first << L": " << pair.second << std::endl;
      }
    }
    m_textRenderer->RenderTextOffscreen(wss.str());

    std::lock_guard<std::mutex> guard(m_coordinateSystemModelLock);
    for (auto& pair : m_coordinateSystemModels)
    {
      if (pair.second.first != nullptr)
      {
        auto result = pair.second.first->TryGetTransformTo(hmdCoordinateSystem);
        pair.second.second->SetDesiredPose(result->Value);
      }
    }
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const std::wstring& value)
  {
    std::lock_guard<std::mutex> guard(m_debugLock);
    m_debugValues[key] = value;
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const float2& value)
  {
    std::wstringstream wss;
    wss << value.x << L" " << value.y;
    std::lock_guard<std::mutex> guard(m_debugLock);
    m_debugValues[key] = wss.str();
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const float3& value)
  {
    std::wstringstream wss;
    wss << value.x << L" " << value.y << L" " << value.z;
    std::lock_guard<std::mutex> guard(m_debugLock);
    m_debugValues[key] = wss.str();
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const float4& value)
  {
    std::wstringstream wss;
    wss << value.x << L" " << value.y << L" " << value.z << L" " << value.w;
    std::lock_guard<std::mutex> guard(m_debugLock);
    m_debugValues[key] = wss.str();
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(const std::wstring& key, const float4x4& value)
  {
    std::wstringstream wss;
    wss << value.m11 << L" " << value.m12 << L" " << value.m13 << L" " << value.m14 << std::endl
        << value.m21 << L" " << value.m22 << L" " << value.m23 << L" " << value.m24 << std::endl
        << value.m31 << L" " << value.m32 << L" " << value.m33 << L" " << value.m34 << std::endl
        << value.m41 << L" " << value.m42 << L" " << value.m43 << L" " << value.m44;
    std::lock_guard<std::mutex> guard(m_debugLock);
    m_debugValues[key] = wss.str();
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, Platform::String^ value)
  {
    UpdateValue(std::wstring(key->Data()), std::wstring(value->Data()));
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, const float2& value)
  {
    UpdateValue(std::wstring(key->Data()), value);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, const float3& value)
  {
    UpdateValue(std::wstring(key->Data()), value);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, const float4& value)
  {
    UpdateValue(std::wstring(key->Data()), value);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateValue(Platform::String^ key, const float4x4& value)
  {
    UpdateValue(std::wstring(key->Data()), value);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateCoordinateSystem(const std::wstring& key, const float4x4& value, SpatialCoordinateSystem^ coordinateSystem)
  {
    std::lock_guard<std::mutex> guard(m_coordinateSystemModelLock);
    if (m_coordinateSystemModels.find(key) == m_coordinateSystemModels.end())
    {
      m_coordinateSystemModels[key] = CoordinateSystemEntry(nullptr, nullptr);
      m_modelRenderer->AddModelAsync(L"Debug\\CoordSystem").then([this, coordinateSystem, key, value](uint64 modelId)
      {
        auto entry = m_modelRenderer->GetModel(modelId);
        if (entry == nullptr)
        {
          LOG_ERROR("Unable to load coordinate system model.");
          return;
        }

        std::lock_guard<std::mutex> guard(m_coordinateSystemModelLock);
        entry->SetCurrentPose(value);
        entry->SetVisible(m_debugShowing);
        m_coordinateSystemModels[key] = CoordinateSystemEntry(coordinateSystem, entry);
      });
    }
    else
    {
      if (coordinateSystem != nullptr)
      {
        m_coordinateSystemModels[key].first = coordinateSystem;
      }
      if (m_coordinateSystemModels[key].second != nullptr)
      {
        m_coordinateSystemModels[key].second->SetDesiredPose(value);
      }
    }
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateCoordinateSystem(const std::wstring& key, const float3& value, SpatialCoordinateSystem^ coordinateSystem)
  {
    UpdateCoordinateSystem(key, make_float4x4_translation(value), coordinateSystem);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateCoordinateSystem(Platform::String^ key, const float4x4& value, SpatialCoordinateSystem^ coordinateSystem)
  {
    UpdateCoordinateSystem(std::wstring(key->Data()), value, coordinateSystem);
  }

  //----------------------------------------------------------------------------
  void Debug::UpdateCoordinateSystem(Platform::String^ key, const float3& value, SpatialCoordinateSystem^ coordinateSystem)
  {
    UpdateCoordinateSystem(std::wstring(key->Data()), make_float4x4_translation(value), coordinateSystem);
  }

  //----------------------------------------------------------------------------
  void Debug::SetModelRenderer(Rendering::ModelRenderer* modelRenderer)
  {
    m_modelRenderer = modelRenderer;

    m_componentReady = m_modelRenderer != nullptr && m_sliceRenderer != nullptr;
  }

  //----------------------------------------------------------------------------
  void Debug::SetSliceRenderer(Rendering::SliceRenderer* sliceRenderer)
  {
    m_sliceRenderer = sliceRenderer;

    if (m_sliceEntry != nullptr)
    {
      m_sliceRenderer->RemoveSlice(m_sliceEntry->GetId());
      m_sliceEntry = nullptr;
    }
    m_sliceRenderer->AddSliceAsync(m_textRenderer->GetTexture(), float4x4::identity(), true).then([this](uint64 entryId)
    {
      m_sliceEntry = m_sliceRenderer->GetSlice(entryId);
      m_sliceEntry->SetVisible(false); // off by default
      m_sliceEntry->SetScalingFactor(0.6f);

      m_componentReady = m_modelRenderer != nullptr && m_sliceRenderer != nullptr;
    });
  }
}