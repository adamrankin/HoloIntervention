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

// STD includes
#include <vector>

// DirectX includes
#include <DirectXMath.h>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Spatial
  {
    struct VertexBufferType
    {
      DirectX::XMFLOAT4 vertex;
    };

    struct IndexBufferType
    {
      uint32 index;
    };

    struct OutputBufferType
    {
      DirectX::XMFLOAT4   intersectionPoint;
      DirectX::XMFLOAT4   intersectionNormal;
      DirectX::XMFLOAT4   intersectionEdge;
      bool                intersection;
    };

    struct WorldConstantBuffer
    {
      DirectX::XMFLOAT4X4 meshToWorld;
    };
    static_assert((sizeof(WorldConstantBuffer) % (sizeof(float) * 4)) == 0, "World constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    struct SurfaceMeshProperties
    {
      unsigned int vertexStride = 0;
      unsigned int indexCount = 0;
      DXGI_FORMAT  indexFormat = DXGI_FORMAT_UNKNOWN;
    };

    class SurfaceMesh
    {
    public:
      SurfaceMesh(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~SurfaceMesh();

      void UpdateSurface(Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ newMesh);
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ GetSurfaceMesh();

      void Update(DX::StepTimer const& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ baseCoordinateSystem);

      void CreateVertexResources();
      void CreateDeviceDependentResources();
      void ReleaseVertexResources();
      void ReleaseDeviceDependentResources();

      bool TestRayOBBIntersection(Windows::Perception::Spatial::SpatialCoordinateSystem^ desiredCoordinateSystem,
                                  uint64_t frameNumber,
                                  const Windows::Foundation::Numerics::float3& rayOrigin,
                                  const Windows::Foundation::Numerics::float3& rayDirection);
      bool TestRayIntersection(ID3D11DeviceContext& context,
                               uint64_t frameNumber,
                               Windows::Foundation::Numerics::float3& outHitPosition,
                               Windows::Foundation::Numerics::float3& outHitNormal,
                               Windows::Foundation::Numerics::float3& outHitEdge);

      bool GetIsActive() const;
      float GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;

      const Windows::Foundation::Numerics::float3& GetLastHitPosition() const;
      const Windows::Foundation::Numerics::float3& GetLastHitNormal() const;
      const Windows::Foundation::Numerics::float3& GetLastHitEdge() const; // this and normal define a coordinate system
      uint64_t GetLastHitFrameNumber() const;

      void SetIsActive(const bool& isActive);

      Windows::Foundation::Numerics::float4x4 GetMeshToWorldTransform();

    protected:
      void SwapVertexBuffers();
      void ComputeOBBInverseWorld(Windows::Perception::Spatial::SpatialCoordinateSystem^ baseCoordinateSystem);

      HRESULT CreateStructuredBuffer(uint32 uStructureSize, Windows::Perception::Spatial::Surfaces::SpatialSurfaceMeshBuffer^ buffer, ID3D11Buffer** target);
      HRESULT CreateStructuredBuffer(uint32 uElementSize, uint32 uCount, ID3D11Buffer** target);
      HRESULT CreateReadbackBuffer(uint32 uElementSize, uint32 uCount);
      HRESULT CreateConstantBuffer();

      HRESULT CreateBufferSRV(Microsoft::WRL::ComPtr<ID3D11Buffer> computeShaderBuffer, ID3D11ShaderResourceView** ppSRVOut);
      HRESULT CreateBufferUAV(Microsoft::WRL::ComPtr<ID3D11Buffer> computeShaderBuffer, ID3D11UnorderedAccessView** ppUAVOut);

      void RunComputeShader(ID3D11DeviceContext& context, uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews, ID3D11UnorderedAccessView* pUnorderedAccessView, uint32 Xthreads, uint32 Ythreads, uint32 Zthreads);

    protected:
      std::shared_ptr<DX::DeviceResources>                          m_deviceResources;

      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^   m_surfaceMesh = nullptr;

      // D3D resources for this mesh
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_vertexPositions = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_triangleIndices = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_updatedVertexPositions = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_updatedTriangleIndices = nullptr;

      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_outputBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_readBackBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_meshConstantBuffer = nullptr;

      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>              m_vertexSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>              m_indexSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>              m_updatedVertexSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>              m_updatedIndicesSRV = nullptr;

      Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>             m_outputUAV = nullptr;

      SurfaceMeshProperties                                         m_meshProperties;
      SurfaceMeshProperties                                         m_updatedMeshProperties;

      // DateTime to allow returning cached ray hits
      Windows::Foundation::DateTime                                 m_lastUpdateTime;

      // Behavior variables
      std::atomic_bool                                              m_vertexLoadingComplete = false;
      std::atomic_bool                                              m_loadingComplete = false;
      std::atomic_bool                                              m_isActive = false;
      std::atomic_bool                                              m_updateNeeded = false;
      std::atomic_bool                                              m_updateReady = false;
      float                                                         m_lastActiveTime = -1.f;

      // Bounding box inverse world matrix
      Windows::Foundation::Numerics::float4x4                       m_worldToBoxCenterTransform = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Perception::Spatial::SpatialCoordinateSystem^        m_lastWorldToBoxComputedCoordSystem = nullptr;

      // Number of indices in the mesh data
      uint32                                                        m_indexCount = 0;

      // Ray-triangle intersection related behavior variables
      std::atomic_bool                                              m_hasLastComputedHit = false;
      Windows::Foundation::Numerics::float3                         m_lastHitPosition;
      Windows::Foundation::Numerics::float3                         m_lastHitNormal;
      Windows::Foundation::Numerics::float3                         m_lastHitEdge;
      uint64                                                        m_lastFrameNumberComputed = 0;
      static const uint32                                           NUMBER_OF_FRAMES_BEFORE_RECOMPUTE = 2; // This translates into FPS/NUMBER_OF_FRAMES_BEFORE_RECOMPUTE recomputations per sec

      Windows::Foundation::Numerics::float4x4                       m_meshToWorldTransform;
      Windows::Foundation::Numerics::float4x4                       m_normalToWorldTransform;

      std::mutex                                                    m_meshResourcesMutex;
    };
  }
}