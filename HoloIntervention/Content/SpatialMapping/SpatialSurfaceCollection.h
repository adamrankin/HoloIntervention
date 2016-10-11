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
    struct RayConstantBuffer
    {
      XMFLOAT4 rayOrigin;
      XMFLOAT4 rayDirection;
    };
    static_assert((sizeof(RayConstantBuffer) % (sizeof(float) * 4)) == 0, "Ray constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    class SpatialSurfaceCollection
    {
      typedef std::map<Platform::Guid, std::shared_ptr<SurfaceMesh> > GuidMeshMap;

    public:
      SpatialSurfaceCollection(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer);
      ~SpatialSurfaceCollection();

      void Update(SpatialCoordinateSystem^ coordinateSystem);

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      bool HasSurface(Platform::Guid id);
      task<void> AddOrUpdateSurfaceAsync(Platform::Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions);
      void RemoveSurface(Platform::Guid id);
      void ClearSurfaces();

      bool TestRayIntersection(SpatialCoordinateSystem^ desiredCoordinateSystem,
                               const float3 rayOrigin,
                               const float3 rayDirection,
                               float3& outHitPosition,
                               float3& outHitNormal,
                               float3& outHitEdge);

      Windows::Foundation::DateTime GetLastUpdateTime(Platform::Guid id);

      void HideInactiveMeshes(Windows::Foundation::Collections::IMapView<Platform::Guid, SpatialSurfaceInfo^>^ const& surfaceCollection);

      bool GetLastHitPosition(_Out_ float3& position, _In_ bool considerOldHits = false);
      bool GetLastHitNormal(_Out_ float3& normal, _In_ bool considerOldHits = false);
      std::shared_ptr<SurfaceMesh> GetLastHitMesh();
      Platform::Guid GetLastHitMeshGuid();

    protected:
      // Cache the step timer for out of date queries
      DX::StepTimer&                                  m_stepTimer;

      Microsoft::WRL::ComPtr<ID3D11Buffer>            m_constantBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ComputeShader>     m_d3d11ComputeShader = nullptr;
      bool                                            m_resourcesLoaded = false;

      // A way to lock map access.
      std::mutex                                      m_meshCollectionLock;

      // Total number of surface meshes.
      unsigned int                                    m_surfaceMeshCount = 0;

      // Cache the latest known mesh to be hit (optimization)
      Platform::Guid                                  m_lastHitMeshGuid;
      std::shared_ptr<SurfaceMesh>                    m_lastHitMesh;

      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>            m_deviceResources = nullptr;

      // The set of surfaces in the collection.
      GuidMeshMap                                     m_meshCollection;

      double                                          m_maxTrianglesPerCubicMeter = 1000.0;

    protected:
      // The duration of time, in seconds, a mesh is allowed to remain inactive before deletion.
      static const float                              MAX_INACTIVE_MESH_TIME_SEC;
      static const uint64_t                           FRAMES_BEFORE_EXPIRED;
    };
  }
}