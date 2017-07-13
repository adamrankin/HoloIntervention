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
#include "InstancedEffectFactory.h"
#include "InstancedEffects.h"

// STL includes
#include <atomic>

namespace DirectX
{
  class CommonStates;
  class IEffect;
  class Model;
  class ModelMesh;
  class ModelMeshPart;
}

namespace DX
{
  class CameraResources;
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    enum ModelRenderingState
    {
      RENDERING_DEFAULT,
      RENDERING_GREYSCALE,
    };

    class ModelEntry
    {
    public:
      ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation, DX::StepTimer& timer);
      ~ModelEntry();

      void Update(const DX::CameraResources* cameraResources);
      void Render();

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void SetVisible(bool enable);
      void ToggleVisible();
      bool IsVisible() const;
      bool IsInFrustum() const;
      bool IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum) const;

      void EnablePoseLerp(bool enable);
      void SetPoseLerpRate(float lerpRate);

      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& world);
      void SetCurrentPose(const Windows::Foundation::Numerics::float4x4& world);
      Windows::Foundation::Numerics::float4x4 GetCurrentPose() const;

      Windows::Foundation::Numerics::float3 GetVelocity() const;

      uint64 GetId() const;
      void SetId(uint64 id);
      std::array<float, 6> GetBounds() const;

      std::wstring GetAssetLocation() const;
      bool GetLerpEnabled() const;
      float GetLerpRate() const;

      // Alternate rendering options
      void RenderGreyscale();
      void RenderDefault();
      void SetWireframe(bool wireframe);
      void SetRenderingState(ModelRenderingState state);
      void EnableLighting(bool enable);
      void SetCullMode(D3D11_CULL_MODE mode);

      bool FailedLoad() const;
      bool IsLoaded() const;

    protected:
      void DrawMesh(_In_ const DirectX::ModelMesh& mesh, _In_ bool alpha, _In_opt_ std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState = nullptr);
      void DrawMeshPart(_In_ const DirectX::ModelMeshPart& part, _In_opt_ std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState = nullptr);
      void CalculateBounds();

      // Update all effects used by the model
      void __cdecl UpdateEffects(_In_ std::function<void __cdecl(DirectX::IEffect*)> setEffect);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources = nullptr;
      const DX::CameraResources*                          m_cameraResources = nullptr;
      DX::StepTimer&                                      m_timer;

      // DirectXTK resources
      std::unique_ptr<DirectX::CommonStates>              m_states = nullptr;
      std::unique_ptr<DirectX::InstancedEffectFactory>    m_effectFactory = nullptr;
      std::shared_ptr<DirectX::Model>                     m_model = nullptr;

      // Frustum checking
      mutable std::atomic_bool                            m_isInFrustum;
      mutable uint64                                      m_frustumCheckFrameNumber;

      // Model state
      std::array<float, 6>                                m_modelBounds = { -1.f };
      std::wstring                                        m_assetLocation;
      std::map<DirectX::IEffect*, DirectX::XMFLOAT4>      m_defaultColours;
      std::atomic_bool                                    m_wireframe = false;
      Windows::Foundation::Numerics::float3               m_velocity = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float4x4             m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_currentPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4             m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();

      // Model behavior
      std::atomic_bool                                    m_visible = false;
      std::atomic_bool                                    m_enableLerp = true;
      float                                               m_poseLerpRate = 4.f;
      uint64                                              m_id = INVALID_TOKEN;

      // Variables used with the rendering loop.
      std::atomic_bool                                    m_loadingComplete = false;
      std::atomic_bool                                    m_failedLoad = false;
    };
  }
}