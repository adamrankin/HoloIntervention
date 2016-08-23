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
#include "SurfaceMesh.h"
#include "StepTimer.h"

// STL includes
#include <memory>
#include <map>

// WinRT includes
#include <ppltasks.h>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace HoloIntervention
{
  namespace Spatial
  {
    class SpatialSurfaceCollection
    {
      typedef std::map<Platform::Guid, std::shared_ptr<SurfaceMesh> > GuidMeshMap;

    public:
      SpatialSurfaceCollection( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~SpatialSurfaceCollection();

      void Update( DX::StepTimer const& timer, SpatialCoordinateSystem^ coordinateSystem );

      Concurrency::task<void> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();

      bool HasSurface( Platform::Guid id );
      void AddSurface( Platform::Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions );
      void UpdateSurface( Platform::Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions );
      void RemoveSurface( Platform::Guid id );
      void ClearSurfaces();

      bool TestRayIntersection( uint64_t frameNumber,
                                SpatialCoordinateSystem^ desiredCoordinateSystem,
                                const float3 rayOrigin,
                                const float3 rayDirection,
                                float3& outHitPosition,
                                float3& outHitNormal );

      Windows::Foundation::DateTime GetLastUpdateTime( Platform::Guid id );

      void HideInactiveMeshes( Windows::Foundation::Collections::IMapView<Platform::Guid, SpatialSurfaceInfo^>^ const& surfaceCollection );

    protected:
      Concurrency::task<HRESULT> CreateComputeShaderAsync( const std::wstring& srcFile );

      HRESULT CreateConstantBuffer();

      Concurrency::task<void> AddOrUpdateSurfaceAsync( Platform::Guid id,
          SpatialSurfaceInfo^ newSurface,
          SpatialSurfaceMeshOptions^ meshOptions );

    protected:
      Microsoft::WRL::ComPtr<ID3D11Buffer>            m_constantBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ComputeShader>     m_d3d11ComputeShader = nullptr;
      bool                                            m_resourcesLoaded = false;
      std::unique_ptr<Concurrency::task<HRESULT>>     m_shaderLoadTask = nullptr;

      // A way to lock map access.
      std::mutex                                      m_meshCollectionLock;

      // Total number of surface meshes.
      unsigned int                                    m_surfaceMeshCount;

      // Level of detail setting. The number of triangles that the system is allowed to provide per cubic meter.
      double                                          m_maxTrianglesPerCubicMeter = 1000.0;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;

      // The duration of time, in seconds, a mesh is allowed to remain inactive before deletion.
      const float                                     MAX_INACTIVE_MESH_TIME_SEC = 120.f;

      // The set of surfaces in the collection.
      GuidMeshMap                                     m_meshCollection;
    };
  }
}