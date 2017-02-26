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

// DirectX includes
#include <dxgiformat.h>
#include <d3d11.h>

// WinRT includes
#include <wrl\client.h>

// STL includes
#include <mutex>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    // Constant buffer used to send hologram position and normal transforms to the shader pipeline.
    struct ModelNormalConstantBuffer
    {
      DirectX::XMFLOAT4X4 modelToWorld;
      DirectX::XMFLOAT4X4 normalToWorld;
      DirectX::XMFLOAT4   colorFadeFactor;
    };

    static_assert((sizeof(ModelNormalConstantBuffer) % (sizeof(float) * 4)) == 0, "Model/normal constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    struct SurfaceMeshProperties
    {
      unsigned int vertexStride = 0;
      unsigned int normalStride = 0;
      unsigned int indexCount = 0;
      DXGI_FORMAT  indexFormat = DXGI_FORMAT_UNKNOWN;
    };

    class Mesh final
    {
    public:
      Mesh();
      Mesh(std::shared_ptr<DX::DeviceResources> deviceResources);
      ~Mesh();

      void SetDeviceResources(std::shared_ptr<DX::DeviceResources> deviceResources);

      void UpdateSurface(Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^ surface);
      void UpdateDeviceBasedResources();
      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ baseCoordinateSystem);

      void Render(bool usingVprtShaders);

      void CreateVertexResources();
      void CreateDeviceDependentResources();
      void ReleaseVertexResources();
      void ReleaseDeviceDependentResources();

      bool GetIsActive() const;
      float GetLastActiveTime() const;
      Windows::Foundation::DateTime GetLastUpdateTime() const;

      void SetIsActive(const bool& isActive);
      void SetColorFadeTimer(const float& duration);

    protected:
      Windows::Perception::Spatial::Surfaces::SpatialSurfaceMesh^   m_surfaceMesh = nullptr;

      // Cached device resources
      std::shared_ptr<DX::DeviceResources>                          m_deviceResources;

      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_vertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_vertexNormals;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_triangleIndices;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_updatedVertexPositions;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_updatedVertexNormals;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_updatedTriangleIndices;
      Microsoft::WRL::ComPtr<ID3D11Buffer>                          m_modelTransformBuffer;

      Windows::Foundation::DateTime                                 m_lastUpdateTime;

      SurfaceMeshProperties                                         m_meshProperties;
      SurfaceMeshProperties                                         m_updatedMeshProperties;

      ModelNormalConstantBuffer                                     m_constantBufferData;

      std::atomic_bool                                              m_constantBufferCreated = false;
      std::atomic_bool                                              m_loadingComplete = false;
      std::atomic_bool                                              m_updateNeeded = false;
      std::atomic_bool                                              m_updateReady = false;
      std::atomic_bool                                              m_isActive = false;
      float                                                         m_lastActiveTime = -1.f;
      float                                                         m_colorFadeTimer = -1.f;
      float                                                         m_colorFadeTimeout = -1.f;

      std::mutex                                                    m_meshResourcesMutex;

    private:
      void SwapVertexBuffers();
      void CreateDirectXBuffer(ID3D11Device& device, D3D11_BIND_FLAG binding, Windows::Storage::Streams::IBuffer^ buffer, ID3D11Buffer** target);
    };
  }
}