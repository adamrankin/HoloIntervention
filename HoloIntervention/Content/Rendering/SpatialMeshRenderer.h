//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"
#include "SpatialMesh.h"

// STL includes
#include <memory>
#include <map>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;
  }

  namespace Rendering
  {
    class SpatialMeshRenderer : public Sound::IVoiceInput, public IEngineComponent
    {
      typedef std::map<Platform::Guid, SpatialMesh> GuidMeshMap;

    public:
      SpatialMeshRenderer(System::NotificationSystem& notificationSystem, const std::shared_ptr<DX::DeviceResources>& deviceResources);

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
      void Render();

      void SetEnabled(bool arg);
      bool GetEnabled() const;

      bool HasSurface(Platform::Guid id);
      void AddSurface(Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface);
      void UpdateSurface(Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface);
      void RemoveSurface(Platform::Guid id);
      void ClearSurfaces();

      Windows::Foundation::DateTime GetLastUpdateTime(Platform::Guid id);

      void HideInactiveMeshes(Windows::Foundation::Collections::IMapView<Platform::Guid, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^>^ const& surfaceCollection);

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Reset();

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      void InitObserver(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
      void RequestAccessAsync(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
      Concurrency::task<void> AddOrUpdateSurfaceAsync(Platform::Guid id, Windows::Perception::Spatial::Surfaces::SpatialSurfaceInfo^ newSurface);
      void OnSurfacesChanged(Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^ sender, Platform::Object^ args);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;
      System::NotificationSystem&                     m_notificationSystem;

      // Direct3D resources for SR mesh rendering pipeline.
      Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>    m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_lightingPixelShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_colorPixelShader;

      // Control variables
      std::atomic_bool                                m_renderEnabled = false;

      GuidMeshMap                                     m_meshCollection;
      std::mutex                                      m_meshCollectionLock;
      unsigned int                                    m_surfaceMeshCount;
      double                                          m_maxTrianglesPerCubicMeter = 1000.0;
      bool                                            m_usingVprtShaders = false;

      Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_defaultRasterizerState;
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_wireframeRasterizerState;
      Windows::Foundation::EventRegistrationToken     m_surfacesChangedToken;
      std::atomic_bool                                m_surfaceAccessAllowed = false;
      std::atomic_bool                                m_spatialPerceptionAccessRequested = false;

      Windows::Perception::Spatial::Surfaces::SpatialSurfaceObserver^               m_surfaceObserver;
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshOptions^            m_surfaceMeshOptions;

      std::atomic_bool                                m_drawWireframe = true;

      const float                                     MAX_INACTIVE_MESH_TIME = 120.f;
      const float                                     SURFACE_MESH_FADE_IN_TIME = 3.0f;
    };
  }
}