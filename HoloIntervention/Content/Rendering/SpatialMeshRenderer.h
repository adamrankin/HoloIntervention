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
#include "DeviceResources.h"
#include "StepTimer.h"
#include "SpatialMesh.h"

// stl includes
#include <memory>
#include <map>

// WinRT includes
#include <ppltasks.h>

// Sound includes
#include "IVoiceInput.h"

using namespace Windows::Foundation::Collections;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    class SpatialMeshRenderer : public Sound::IVoiceInput
    {
      typedef std::map<Platform::Guid, SpatialMesh> GuidMeshMap;

    public:
      SpatialMeshRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);

      void Update(DX::ViewProjection& vp, DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem);
      void Render();

      void SetEnabled(bool arg);
      bool GetEnabled() const;

      bool HasSurface(Platform::Guid id);
      void AddSurface(Platform::Guid id, Surfaces::SpatialSurfaceInfo^ newSurface);
      void UpdateSurface(Platform::Guid id, Surfaces::SpatialSurfaceInfo^ newSurface);
      void RemoveSurface(Platform::Guid id);
      void ClearSurfaces();

      Windows::Foundation::DateTime GetLastUpdateTime(Platform::Guid id);

      void HideInactiveMeshes(IMapView<Platform::Guid, Surfaces::SpatialSurfaceInfo^>^ const& surfaceCollection);

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void DebugDrawBoundingBox(int32_t index);
      void DebugDrawBoundingBoxes(bool draw);
      void DebugLoopThroughMeshes();

      void Reset();

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      void InitObserver(SpatialCoordinateSystem^ coordinateSystem);
      void RequestAccessAsync(SpatialCoordinateSystem^ coordinateSystem);
      task<void> AddOrUpdateSurfaceAsync(Platform::Guid id, Surfaces::SpatialSurfaceInfo^ newSurface);
      void OnSurfacesChanged(Surfaces::SpatialSurfaceObserver^ sender, Platform::Object^ args);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;

      // Direct3D resources for SR mesh rendering pipeline.
      Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>    m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_lightingPixelShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_colorPixelShader;

      // Control variables
      bool                                            m_renderEnabled = false;

      // Looping related variables
      bool                                            m_isLooping = false;
      size_t                                          m_currentLoopIndex = 0;
      float                                           m_loopTimer = 0.f;
      bool                                            m_drawSingleMesh = false;
      Platform::Guid                                  m_drawSingleMeshGuid;

      // Bounding box debug related variables
      bool                                            m_debugBoundingBox;
      int32_t                                         m_overrideDrawIndex = -1;

      // The set of surfaces in the collection.
      GuidMeshMap                                     m_meshCollection;

      // A way to lock map access.
      std::mutex                                      m_meshCollectionLock;

      // Total number of surface meshes.
      unsigned int                                    m_surfaceMeshCount;

      // Level of detail setting. The number of triangles that the system is allowed to provide per cubic meter.
      double                                          m_maxTrianglesPerCubicMeter = 1000.0;

      // If the current D3D Device supports VPRT, we can avoid using a geometry
      // shader just to set the render target array index.
      bool                                            m_usingVprtShaders = false;

      // Rasterizer states, for different rendering modes.
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_defaultRasterizerState;
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_wireframeRasterizerState;

      // Event tokens
      Windows::Foundation::EventRegistrationToken     m_surfacesChangedToken;

      // Indicates whether access to spatial mapping data has been granted.
      bool                                            m_surfaceAccessAllowed = false;

      // Indicates whether the surface observer initialization process was started.
      bool                                            m_spatialPerceptionAccessRequested = false;

      // Obtains spatial mapping data from the device in real time.
      Surfaces::SpatialSurfaceObserver^               m_surfaceObserver;
      Surfaces::SpatialSurfaceMeshOptions^            m_surfaceMeshOptions;

      // Determines the rendering mode.
      bool                                            m_drawWireframe = true;

      // The duration of time, in seconds, a mesh is allowed to remain inactive before deletion.
      const float                                     c_maxInactiveMeshTime = 120.f;

      // The duration of time, in seconds, taken for a new surface mesh to fade in on-screen.
      const float                                     c_surfaceMeshFadeInTime = 3.0f;

      bool                                            m_loadingComplete;
    };
  }
}