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

namespace DX
{
  struct ViewProjectionConstantBuffer
  {
    DirectX::XMFLOAT4   cameraPosition[2];
    DirectX::XMFLOAT4   lightPosition[2];
    DirectX::XMFLOAT4X4 view[2];
    DirectX::XMFLOAT4X4 projection[2];
    DirectX::XMFLOAT4X4 viewProjection[2];
  };

  static_assert((sizeof(ViewProjectionConstantBuffer) % (sizeof(float) * 4)) == 0, "View/projection constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

  class DeviceResources;

  class CameraResources
  {
  public:
    CameraResources(Windows::Graphics::Holographic::HolographicCamera^ holographicCamera);

    void CreateResourcesForBackBuffer(DX::DeviceResources* pDeviceResources, Windows::Graphics::Holographic::HolographicCameraRenderingParameters^ cameraParameters);
    void ReleaseResourcesForBackBuffer(DX::DeviceResources* pDeviceResources);

    bool Update(std::shared_ptr<DX::DeviceResources> deviceResources,
                Windows::Graphics::Holographic::HolographicCameraPose^ cameraPose,
                Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
    bool Attach(std::shared_ptr<DX::DeviceResources> deviceResources);

    ViewProjectionConstantBuffer GetLatestViewProjectionBuffer() const;
    bool GetLatestSpatialBoundingFrustum(Windows::Perception::Spatial::SpatialBoundingFrustum& outFrustum) const;

    // Direct3D device resources.
    ID3D11RenderTargetView* GetBackBufferRenderTargetView() const;
    ID3D11DepthStencilView* GetDepthStencilView() const;
    ID3D11Texture2D* GetBackBufferTexture2D() const;
    D3D11_VIEWPORT GetViewport() const;
    DXGI_FORMAT GetBackBufferDXGIFormat() const;

    // Render target properties.
    Windows::Foundation::Size GetRenderTargetSize() const;
    bool IsRenderingStereoscopic() const;

    // The holographic camera these resources are for.
    Windows::Graphics::Holographic::HolographicCamera^ GetHolographicCamera() const;

  private:
    // Direct3D rendering objects. Required for 3D.
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>        m_d3dRenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>        m_d3dDepthStencilView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>               m_d3dBackBuffer;

    // Device resource to store view and projection matrices.
    Microsoft::WRL::ComPtr<ID3D11Buffer>                  m_viewProjectionConstantBuffer;

    // CPU side resource to store view and projection matrices.
    ViewProjectionConstantBuffer                                            m_cpuViewProjectionConstantBuffer;
    std::shared_ptr<Windows::Perception::Spatial::SpatialBoundingFrustum>   m_spatialBoundingFrustum = nullptr;

    // Direct3D rendering properties.
    DXGI_FORMAT                                           m_dxgiFormat;
    Windows::Foundation::Size                             m_d3dRenderTargetSize;
    D3D11_VIEWPORT                                        m_d3dViewport;

    // Indicates whether the camera supports stereoscopic rendering.
    std::atomic_bool                                      m_isStereo = true;

    // Indicates whether this camera has a pending frame.
    std::atomic_bool                                      m_framePending = false;

    // Pointer to the holographic camera these resources are for.
    Windows::Graphics::Holographic::HolographicCamera^    m_holographicCamera = nullptr;
  };
}
