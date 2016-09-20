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
#include "DeviceResources.h"
#include "StepTimer.h"

// STD includes
#include <vector>

// DirectX includes
#include <DirectXMath.h>

using namespace DirectX;
using namespace Concurrency;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;

namespace HoloIntervention
{
  namespace Spatial
  {
    struct VertexBufferType
    {
      XMFLOAT4 vertex;
    };

    struct IndexBufferType
    {
      uint32 index;
    };

    struct OutputBufferType
    {
      XMFLOAT4 intersectionPoint;
      XMFLOAT4 intersectionNormal;
    };

    struct WorldConstantBuffer
    {
      // Constant buffers must have a a ByteWidth multiple of 16
      DirectX::XMFLOAT4X4 meshToWorld;
    };
    static_assert( ( sizeof( WorldConstantBuffer ) % ( sizeof( float ) * 4 ) ) == 0, "World constant buffer size must be 16-byte aligned (16 bytes is the length of four floats)." );

    class SurfaceMesh
    {
    public:
      SurfaceMesh( const std::shared_ptr<DX::DeviceResources>& deviceResources, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions );
      ~SurfaceMesh();

      void UpdateSurface( SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions );

      void Update( DX::StepTimer const& timer,
                            SpatialCoordinateSystem^ baseCoordinateSystem );

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      bool TestRayOBBIntersection( SpatialCoordinateSystem^ desiredCoordinateSystem,
                                   uint64_t frameNumber,
                                   const float3& rayOrigin,
                                   const float3& rayDirection );

      bool TestRayIntersection( ID3D11DeviceContext& context,
                                uint64_t frameNumber,
                                float3& outHitPosition,
                                float3& outHitNormal );

      const bool& GetIsActive() const;
      const float& GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;
      const float3& GetLastHitPosition() const;
      uint64_t GetLastHitFrameNumber() const;

      void SetIsActive( const bool& isActive );

    protected:
      void UpdateDeviceBasedResources();

      void ComputeOBBInverseWorld(SpatialCoordinateSystem^ baseCoordinateSystem);

      HRESULT CreateStructuredBuffer( uint32 uStructureSize,
                                      SpatialSurfaceMeshBuffer^ buffer,
                                      ID3D11Buffer** target );

      HRESULT CreateStructuredBuffer( uint32 uElementSize,
                                      uint32 uCount,
                                      ID3D11Buffer** target );

      HRESULT CreateReadbackBuffer( uint32 uElementSize,
                                    uint32 uCount );

      HRESULT CreateBufferSRV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                               SpatialSurfaceMeshBuffer^ buffer,
                               ID3D11ShaderResourceView** ppSRVOut );

      HRESULT CreateBufferUAV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                               ID3D11UnorderedAccessView** ppUAVOut );

      HRESULT CreateConstantBuffer();

      void RunComputeShader( ID3D11DeviceContext& context,
                             uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                             ID3D11UnorderedAccessView* pUnorderedAccessView,
                             uint32 Xthreads, uint32 Ythreads, uint32 Zthreads );

    protected:
      // Cache a pointer to the d3d device resources
      std::shared_ptr<DX::DeviceResources>  m_deviceResources;

      // The mesh owned by this object
      SpatialSurfaceMeshOptions^            m_meshOptions = nullptr;
      SpatialSurfaceInfo^                   m_surfaceInfo = nullptr;
      SpatialSurfaceMesh^                   m_surfaceMesh = nullptr;

      // D3D resources for this mesh
      ComPtr<ID3D11Buffer>                  m_vertexPositionBuffer = nullptr;
      ComPtr<ID3D11Buffer>                  m_indexBuffer = nullptr;
      ComPtr<ID3D11Buffer>                  m_outputBuffer = nullptr;
      ComPtr<ID3D11Buffer>                  m_readBackBuffer = nullptr;
      ComPtr<ID3D11Buffer>                  m_meshConstantBuffer = nullptr;

      ComPtr<ID3D11ShaderResourceView>      m_meshSRV = nullptr;
      ComPtr<ID3D11ShaderResourceView>      m_indexSRV = nullptr;
      ComPtr<ID3D11UnorderedAccessView>     m_outputUAV = nullptr;

      // DateTime to allow returning cached ray hits
      Windows::Foundation::DateTime         m_lastUpdateTime;

      // Behavior variables
      bool                                  m_loadingComplete = false;
      bool                                  m_isActive = false;
      float                                 m_lastActiveTime = -1.f;
      double                                m_maxTrianglesPerCubicMeter = 1000.0;

      // Bounding box inverse world matrix
      XMMATRIX                              m_worldToBoxTransform;
      bool                                  m_worldToBoxTransformComputed = false;

      // Number of indices in the mesh data
      uint32                                m_indexCount = 0;

      // Ray-triangle intersection related behavior variables
      bool                                  m_hasLastComputedHit = false;
      float3                                m_rayIntersectionResultPosition;
      float3                                m_rayIntersectionResultNormal;
      uint64                                m_lastFrameNumberComputed = 0;
      static const uint32                   NUMBER_OF_FRAMES_BEFORE_RECOMPUTE = 2; // This translates into FPS/NUMBER_OF_FRAMES_BEFORE_RECOMPUTE recomputations per sec

      DirectX::XMFLOAT4X4                   m_meshToWorldTransform;
      DirectX::XMFLOAT4X4                   m_normalToWorldTransform;

      std::mutex                            m_meshResourcesMutex;
    };
  }
}