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
    class Slice;
    class SliceRenderer;
    class TextRenderer;
    class ModelRenderer;
    class Model;
  }

  // Class requires SetModelRenderer and SetSliceRenderer to be called post-construction (circular dependency)
  class Debug : public Input::IVoiceInput, public IEngineComponent
  {
    typedef std::pair<Windows::Perception::Spatial::SpatialCoordinateSystem^, std::shared_ptr<Rendering::Model>> CoordinateSystemEntry;
    typedef std::map<std::wstring, CoordinateSystemEntry> CoordinateSystemMap;

  public:
    Debug(Rendering::SliceRenderer& sliceRenderer, const std::shared_ptr<DX::DeviceResources>& deviceResources);
    ~Debug();

  public:
    // IVoiceInput functions
    virtual void RegisterVoiceCallbacks(HoloIntervention::Input::VoiceInputCallbackMap& callbackMap);

  public:
    void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem);

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

    void UpdateCoordinateSystem(const std::wstring& key, const Windows::Foundation::Numerics::float4x4& value, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem = nullptr);
    void UpdateCoordinateSystem(const std::wstring& key, const Windows::Foundation::Numerics::float3& value, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem = nullptr);
    void UpdateCoordinateSystem(Platform::String^ key, const Windows::Foundation::Numerics::float4x4& value, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem = nullptr);
    void UpdateCoordinateSystem(Platform::String^ key, const Windows::Foundation::Numerics::float3& value, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem = nullptr);

    void SetModelRenderer(Rendering::ModelRenderer* renderer);
    void SetSliceRenderer(Rendering::SliceRenderer* renderer);

  protected:
    // Cached variables
    Rendering::SliceRenderer*                                       m_sliceRenderer = nullptr;
    Rendering::ModelRenderer*                                       m_modelRenderer = nullptr;

    // Text rendering variables
    std::unique_ptr<Rendering::TextRenderer>                        m_textRenderer;

    // Class variables
    std::mutex                                                      m_debugLock;
    std::map<std::wstring, std::wstring>                            m_debugValues;
    std::atomic_bool                                                m_worldLocked = false;
    std::shared_ptr<Rendering::Slice>                          m_sliceEntry = nullptr;
    std::atomic_bool                                                m_debugShowing = false;

    // Coordinate system debugging
    std::mutex                                                      m_coordinateSystemModelLock;
    CoordinateSystemMap                                             m_coordinateSystemModels;
  };
}