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

using namespace Windows::Storage::Streams;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

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

      bool TestRayIntersection( ID3D11DeviceContext& context,
                                ID3D11ComputeShader& computeShader,
                                const DX::StepTimer& timer,
                                std::vector<double>& outResult );

      const bool& GetIsActive() const;
      const float& GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;

      void SetIsActive( const bool& isActive );

    private:
      concurrency::task<void> UpdateDeviceBasedResourcesAsync( ID3D11Device* device );

      concurrency::task<void> ComputeAndStoreOBBAsync();

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uElementSize,
                                      IBuffer^ buffer,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uElementSize,
                                      uint32 uCount,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateBufferSRV( ID3D11Device* pDevice,
                               ID3D11Buffer* pBuffer,
                               ID3D11ShaderResourceView** ppSRVOut );

      HRESULT CreateBufferUAV( ID3D11Device* pDevice,
                               ID3D11Buffer* pBuffer,
                               ID3D11UnorderedAccessView** pUAVOut );

      void RunComputeShader( ID3D11DeviceContext& context,
                             ID3D11ComputeShader& computeShader,
                             uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                             ID3D11UnorderedAccessView* pUnorderedAccessView,
                             uint32 Xthreads, uint32 Ythreads, uint32 Zthreads );

    private:
      SpatialSurfaceMesh^                                 m_surfaceMesh = nullptr;

      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_meshBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_indexBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_outputBuffer = nullptr;

      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_meshSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_indexSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>   m_outputUAV = nullptr;

      struct InputBufferType
      {
        double vertexOne[3];
        double vertexTwo[3];
        double vertexThree[3];
      };

      struct OutputBufferType
      {
        double intersectionPoint[3];
      };

      unsigned int                    m_vertexStride = 0;
      unsigned int                    m_normalStride = 0;

      Windows::Foundation::DateTime   m_lastUpdateTime;

      bool                            m_loadingComplete = false;
      bool                            m_isActive = false;
      float                           m_lastActiveTime = -1.f;

      unsigned int                    m_indexCount = 0;

      // Ray-triangle intersection related behavior variables
      bool                            m_rayComputed = false;
      double                          m_rayIntersectionPoint[3] = { 0.0, 0.0, 0.0 };
      unsigned long                   m_lastFrameNumberComputed = 0;
      const unsigned int              NUMBER_OF_FRAMES_BEFORE_RECOMPUTE = 2;

      DirectX::XMFLOAT4X4             m_meshToWorldTransform;
      DirectX::XMFLOAT4X4             m_normalToWorldTransform;

      std::mutex                      m_meshResourcesMutex;
    };
  }
}