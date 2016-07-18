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

      void UpdateSurface( SpatialSurfaceMesh^ surface );
      void UpdateDeviceBasedResources( ID3D11Device* device );
      void UpdateTransform( ID3D11DeviceContext* context, DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem );

      void CreateVertexResources( ID3D11Device* device );
      void CreateDeviceDependentResources( ID3D11Device* device );
      void ReleaseVertexResources();
      void ReleaseDeviceDependentResources();

      const bool& GetIsActive() const;
      const float& GetLastActiveTime() const;
      const Windows::Foundation::DateTime& GetLastUpdateTime() const;

      void SetIsActive( const bool& isActive );

    private:
      void GetUpdatedVertexResources();
      void CreateDirectXBuffer( ID3D11Device* device, D3D11_BIND_FLAG binding, Windows::Storage::Streams::IBuffer^ buffer, ID3D11Buffer** target );

      SpatialSurfaceMesh^ m_surfaceMesh = nullptr;

      Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer> m_vertexNormals;
      Microsoft::WRL::ComPtr<ID3D11Buffer> m_triangleIndices;
      Microsoft::WRL::ComPtr<ID3D11Buffer> m_updatedVertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer> m_updatedVertexNormals;
      Microsoft::WRL::ComPtr<ID3D11Buffer> m_updatedTriangleIndices;

      Windows::Foundation::DateTime m_lastUpdateTime;

      unsigned int m_vertexStride = 0;
      unsigned int m_normalStride = 0;
      DXGI_FORMAT  m_indexFormat = DXGI_FORMAT_UNKNOWN;

      bool   m_loadingComplete = false;
      bool   m_updateNeeded = false;
      bool   m_updateReady = false;
      bool   m_isActive = false;
      float  m_lastActiveTime = -1.f;
      uint32 m_indexCount = 0;

      DirectX::XMFLOAT4X4 m_meshToWorldTransform;
      DirectX::XMFLOAT4X4 m_normalToWorldTransform;

      std::mutex m_meshResourcesMutex;
    };
  }
}