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
#include "SurfaceMesh.h"
#include "StepTimer.h"

// STL includes
#include <memory>
#include <map>

// WinRT includes
#include <ppltasks.h>

using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    class SpatialSurfaceCollection
    {
    public:
      SpatialSurfaceCollection( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      void Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem );

      bool HasSurface( Platform::Guid id );
      void AddSurface( Platform::Guid id, SpatialSurfaceInfo^ newSurface );
      void UpdateSurface( Platform::Guid id, SpatialSurfaceInfo^ newSurface );
      void RemoveSurface( Platform::Guid id );
      void ClearSurfaces();

      bool TestRayIntersection( const DX::StepTimer& timer,
                                const std::vector<double>& origin,
                                const std::vector<double>& direction,
                                std::vector<double>& outResult );

      Windows::Foundation::DateTime GetLastUpdateTime( Platform::Guid id );

      void HideInactiveMeshes( Windows::Foundation::Collections::IMapView<Platform::Guid, SpatialSurfaceInfo^>^ const& surfaceCollection );

    private:
      concurrency::task<HRESULT> CreateComputeShaderAsync( const std::wstring& srcFile,
          ID3D11Device* pDevice,
          ID3D11ComputeShader** ppShaderOut );

      HRESULT CreateConstantBuffer( ID3D11Device* device );

      void SetConstants( ID3D11DeviceContext* context, const std::vector<double>& rayOrigin, const std::vector<double>& rayDirection );

      Concurrency::task<void> AddOrUpdateSurfaceAsync( Platform::Guid id,
          SpatialSurfaceInfo^ newSurface );

    private:
      Microsoft::WRL::ComPtr<ID3D11Buffer>            m_constantBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ComputeShader>     m_d3d11ComputeShader = nullptr;
      bool                                            m_shaderLoaded = false;
      std::unique_ptr<concurrency::task<HRESULT>>     m_shaderLoadTask = nullptr;

      struct ConstantBuffer
      {
        double rayOrigin[3];
        double rayDirection[3];
      };

      // The set of surfaces in the collection.
      std::map<Platform::Guid, SurfaceMesh>           m_meshCollection;

      // A way to lock map access.
      std::mutex                                      m_meshCollectionLock;

      // Total number of surface meshes.
      unsigned int                                    m_surfaceMeshCount;

      // Level of detail setting. The number of triangles that the system is allowed to provide per cubic meter.
      double                                          m_maxTrianglesPerCubicMeter = 1000.0;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;

      // The duration of time, in seconds, a mesh is allowed to remain inactive before deletion.
      const float                                     c_maxInactiveMeshTime = 120.f;
    };
  }
}