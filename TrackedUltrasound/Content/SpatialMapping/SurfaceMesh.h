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
#include "ShaderStructures.h"
#include "StepTimer.h"

// STD includes
#include <vector>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    class SurfaceMesh final
    {
    public:
      SurfaceMesh();
      ~SurfaceMesh();

      void UpdateSurface( SpatialSurfaceMesh^ surface,
                          ID3D11Device* device );

      void UpdateTransform( ID3D11DeviceContext* context,
                            DX::StepTimer const& timer,
                            SpatialCoordinateSystem^ baseCoordinateSystem );

      void CreateVertexResources( ID3D11Device* device );
      void CreateDeviceDependentResources( ID3D11Device* device );
      void ReleaseVertexResources();
      void ReleaseDeviceDependentResources();

      bool TestRayIntersection( ID3D11Device& device,
                                ID3D11DeviceContext& context,
                                ID3D11ComputeShader& computeShader,
                                uint64_t frameNumber,
                                std::vector<float>& outHitPosition,
                                std::vector<float>& outHitNormal );

      const bool& GetIsActive() const;
      const float& GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;

      void SetIsActive( const bool& isActive );

      void SetRayConstants(ID3D11DeviceContext* context,
        ID3D11Buffer* constantBuffer,
        const float3 rayOrigin,
        const float3 rayDirection);

    private:
      concurrency::task<void> UpdateDeviceBasedResourcesAsync( ID3D11Device* device );

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uStructureSize,
                                      SpatialSurfaceMeshBuffer^ buffer,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uElementSize,
                                      uint32 uCount,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateReadbackBuffer( ID3D11Device* pDevice,
                                    uint32 uElementSize,
                                    uint32 uCount,
                                    ID3D11Buffer** ppBufOut );

      HRESULT CreateBufferSRV( ID3D11Device& pDevice,
                               ID3D11Buffer& computeShaderBuffer,
                               SpatialSurfaceMeshBuffer^ buffer,
                               ID3D11ShaderResourceView** ppSRVOut );

      HRESULT CreateBufferUAV( ID3D11Device& device,
                               ID3D11Buffer& computeShaderBuffer,
                               ID3D11UnorderedAccessView** pUAVOut );

      void RunComputeShader( ID3D11DeviceContext& context,
                             ID3D11ComputeShader& computeShader,
                             uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                             ID3D11UnorderedAccessView* pUnorderedAccessView,
                             uint32 Xthreads, uint32 Ythreads, uint32 Zthreads );

    private:
      SpatialSurfaceMesh^          m_surfaceMesh = nullptr;

      ID3D11Buffer*                m_vertexPositionBuffer = nullptr;
      ID3D11Buffer*                m_indexBuffer = nullptr;
      ID3D11Buffer*                m_outputBuffer = nullptr;
      ID3D11Buffer*                m_readBackBuffer = nullptr;

      ID3D11ShaderResourceView*    m_meshSRV = nullptr;
      ID3D11ShaderResourceView*    m_indexSRV = nullptr;
      ID3D11UnorderedAccessView*   m_outputUAV = nullptr;

      uint32                          m_vertexStride = 0;
      uint32                          m_normalStride = 0;

      Windows::Foundation::DateTime   m_lastUpdateTime;

      bool                            m_loadingComplete = false;
      bool                            m_isActive = false;
      float                           m_lastActiveTime = -1.f;

      uint32                          m_indexCount = 0;

      // Ray-triangle intersection related behavior variables
      bool                            m_hasLastComputedHit = false;
      std::vector<float>              m_rayIntersectionResults;
      uint64                          m_lastFrameNumberComputed = 0;
      const uint32                    NUMBER_OF_FRAMES_BEFORE_RECOMPUTE = 2; // This translates into FPS/NUMBER_OF_FRAMES_BEFORE_RECOMPUTE recomputations per sec

      DirectX::XMFLOAT4X4             m_meshToWorldTransform;
      DirectX::XMFLOAT4X4             m_normalToWorldTransform;

      std::mutex                      m_meshResourcesMutex;
    };
  }
}