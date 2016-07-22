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

      void UpdateSurface( SpatialSurfaceMesh^ surface, ID3D11Device* device );
      void UpdateTransform( ID3D11DeviceContext* context, DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem );

      void CreateVertexResources( ID3D11Device* device );
      void CreateDeviceDependentResources( ID3D11Device* device );
      void ReleaseVertexResources();
      void ReleaseDeviceDependentResources();

      // TODO : implement intersect function using compute shader

      const bool& GetIsActive() const;
      const float& GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;

      void SetIsActive( const bool& isActive );

    private:
      concurrency::task<void> UpdateDeviceBasedResourcesAsync(ID3D11Device* device);

      concurrency::task<void> ComputeAndStoreOBBAsync();

      void SetConstants(ID3D11DeviceContext* context, const std::vector<double>& rayOrigin, const std::vector<double>& rayDirection);

      concurrency::task<HRESULT> SurfaceMesh::CreateComputeShaderAsync( const std::wstring& srcFile,
                                   const std::string& functionName,
                                   ID3D11Device* pDevice,
                                   ID3D11ComputeShader** ppShaderOut );

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uElementSize,
                                      IBuffer^ buffer,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateStructuredBuffer( ID3D11Device* pDevice,
                                      uint32 uElementSize,
                                      uint32 uCount,
                                      ID3D11Buffer** ppBufOut );

      HRESULT CreateConstantBuffer(ID3D11Device* device);

      HRESULT CreateBufferSRV( ID3D11Device* pDevice,
                               ID3D11Buffer* pBuffer,
                               ID3D11ShaderResourceView** ppSRVOut );

      HRESULT CreateBufferUAV( ID3D11Device* pDevice,
                               ID3D11Buffer* pBuffer,
                               ID3D11UnorderedAccessView** pUAVOut );

      void RunComputeShader( ID3D11DeviceContext* pContext,
                             ID3D11ComputeShader* pComputeShader,
                             uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                             ID3D11Buffer* pCBCS, void* pCSData, DWORD dwNumDataBytes,
                             ID3D11UnorderedAccessView* pUnorderedAccessView,
                             uint32 Xthreads, uint32 Ythreads, uint32 Zthreads );

    private:
      SpatialSurfaceMesh^                                 m_surfaceMesh = nullptr;

      Microsoft::WRL::ComPtr<ID3D11ComputeShader>         m_d3d11ComputeShader = nullptr;

      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_meshBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_normalBuffer = nullptr;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_outputBuffer = nullptr;

      Microsoft::WRL::ComPtr<ID3D11Buffer>                m_constantBuffer = nullptr;

      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_meshSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_normalSRV = nullptr;
      Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>   m_outputUAV = nullptr;

      struct ConstantBuffer
      {
        double rayOrigin[3];
        double rayDirection[3];
      };

      struct InputBufferType
      {
        double vertexPosition[3];
        double vertexNormal[3];
      };

      struct OutputBufferType
      {
        double intersectionPoint[3];
      };

      unsigned int m_vertexStride;
      unsigned int m_normalStride;

      Windows::Foundation::DateTime m_lastUpdateTime;

      bool   m_loadingComplete = false;
      bool   m_isActive = false;
      float  m_lastActiveTime = -1.f;

      DirectX::XMFLOAT4X4 m_meshToWorldTransform;
      DirectX::XMFLOAT4X4 m_normalToWorldTransform;

      std::mutex m_meshResourcesMutex;
    };
  }
}