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
#include "Common.h"
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

namespace DirectX
{
  class IEffectFactory;
  class InstancedGeometricPrimitive;

  std::unique_ptr<DirectX::Model> CreateFromPolyData(ID3D11Device* d3dDevice, IEffectFactory& fxFactory, UWPOpenIGTLink::Polydata^ polyData);
}

namespace HoloIntervention
{
  class Debug;

  namespace Rendering
  {
    enum ModelRenderingState
    {
      RENDERING_DEFAULT,
      RENDERING_GREYSCALE,
    };

    class Model
    {
    public:
      Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, UWPOpenIGTLink::Polydata^ polydata, DX::StepTimer& timer, Debug& debug);
      Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation, DX::StepTimer& timer, Debug& debug);
      Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, PrimitiveType type, DX::StepTimer& timer, Debug& debug, Windows::Foundation::Numerics::float3 argument, size_t tessellation, bool rhcoords, bool invertn, Windows::Foundation::Numerics::float4 colour = Windows::Foundation::Numerics::float4(1.f, 1.f, 1.f, 1.f));
      ~Model();
      std::shared_ptr<Model> Clone();

      void Update(const DX::CameraResources* cameraResources);
      void Render();

      // DirectX Resources
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Visibility functionality
      void SetVisible(bool enable);
      void ToggleVisible();
      bool IsVisible() const;
      bool IsInFrustum() const;
      bool IsInFrustum(const Windows::Perception::Spatial::SpatialBoundingFrustum& frustum) const;

      // Pose interpolation controls
      void EnablePoseLerp(bool enable);
      void SetPoseLerpRate(float lerpRate);

      // Pose controls
      void SetDesiredPose(const Windows::Foundation::Numerics::float4x4& world);
      void SetCurrentPose(const Windows::Foundation::Numerics::float4x4& world);
      Windows::Foundation::Numerics::float4x4 GetCurrentPose() const;

      Windows::Foundation::Numerics::float3 GetVelocity() const;

      // Unique ID
      uint64 GetId() const;
      void SetId(uint64 id);

      // Bounds
      std::array<float, 6> GetBounds(Windows::Foundation::Numerics::float4x4 userMatrix = Windows::Foundation::Numerics::float4x4::identity()) const;
      void CalculateBounds();

      // Primitive controls
      bool IsPrimitive() const;
      PrimitiveType GetPrimitiveType() const;
      Windows::Foundation::Numerics::float3 GetArgument() const;
      size_t GetTessellation() const;
      bool GetRHCoords() const;
      bool GetInvertN() const;
      bool GetLerpEnabled() const;
      float GetLerpRate() const;

      // Alternate rendering options
      void RenderGreyscale();
      void RenderDefault();
      void SetWireframe(bool wireframe);
      void SetRenderingState(ModelRenderingState state);
      void EnableLighting(bool enable);
      void SetCullMode(D3D11_CULL_MODE mode);

      // Asset controls
      std::wstring GetAssetLocation() const;
      bool FailedLoad() const;
      bool IsLoaded() const;

      // Colour control
      void SetColour(Windows::Foundation::Numerics::float4 newColour);
      void SetColour(Windows::Foundation::Numerics::float3 newColour);
      void SetColour(float r, float g, float b, float a);
      void SetColour(float r, float g, float b);
      Windows::Foundation::Numerics::float4 GetCurrentColour() const;
      // Original colour control (primitives only)
      void SetOriginalColour(Windows::Foundation::Numerics::float4 newColour);
      void SetOriginalColour(Windows::Foundation::Numerics::float3 newColour);
      void SetOriginalColour(float r, float g, float b, float a);
      void SetOriginalColour(float r, float g, float b);
      Windows::Foundation::Numerics::float4 GetOriginalColour() const;

    protected:
      void DrawMesh(_In_ const DirectX::ModelMesh& mesh, _In_ bool alpha, _In_opt_ std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState = nullptr);
      void DrawMeshPart(_In_ const DirectX::ModelMeshPart& part, _In_opt_ std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState = nullptr);

      // Update all effects used by the model
      void __cdecl UpdateEffects(_In_ std::function<void __cdecl(DirectX::IEffect*)> setEffect);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                  m_deviceResources = nullptr;
      const DX::CameraResources*                            m_cameraResources = nullptr;
      DX::StepTimer&                                        m_timer;
      Debug&                                                m_debug;

      // DirectXTK resources
      std::unique_ptr<DirectX::CommonStates>                m_states = nullptr;
      std::unique_ptr<DirectX::InstancedEffectFactory>      m_effectFactory = nullptr;
      std::shared_ptr<DirectX::Model>                       m_model = nullptr;

      // Frustum checking
      mutable std::atomic_bool                              m_isInFrustum;
      mutable uint64                                        m_frustumCheckFrameNumber;

      // Primitive resources
      PrimitiveType                                         m_primitiveType = PrimitiveType_NONE;
      Windows::Foundation::Numerics::float3                 m_argument;
      size_t                                                m_tessellation;
      bool                                                  m_rhcoords;
      bool                                                  m_invertn;
      std::unique_ptr<DirectX::InstancedGeometricPrimitive> m_primitive = nullptr;
      Windows::Foundation::Numerics::float4                 m_currentColour = { 1.f, 1.f, 1.f, 1.f };
      Windows::Foundation::Numerics::float4                 m_originalColour = { 1.f, 1.f, 1.f, 1.f };

      // Model state
      UWPOpenIGTLink::Polydata^                             m_polydata = nullptr;
      std::array<float, 6>                                  m_modelBounds = { -1.f };
      std::wstring                                          m_assetLocation;
      std::map<DirectX::IEffect*, DirectX::XMFLOAT4>        m_defaultColours;
      std::atomic_bool                                      m_isGreyscale = false;
      std::atomic_bool                                      m_wireframe = false;
      Windows::Foundation::Numerics::float3                 m_velocity = { 0.f, 0.f, 0.f };
      Windows::Foundation::Numerics::float4x4               m_lastPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4               m_currentPose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4               m_desiredPose = Windows::Foundation::Numerics::float4x4::identity();

      // Model behavior
      std::atomic_bool                                      m_visible = false;
      std::atomic_bool                                      m_enableLerp = true;
      float                                                 m_poseLerpRate = 4.f;
      uint64                                                m_id = INVALID_TOKEN;

      // Variables used with the rendering loop.
      std::atomic_bool                                      m_loadingComplete = false;
      std::atomic_bool                                      m_failedLoad = false;
    };
  }
}