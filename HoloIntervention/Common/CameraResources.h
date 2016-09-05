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

using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;

namespace DX
{
  // Constant buffer used to send the view-projection matrices to the shader pipeline.
  struct ViewProjectionConstantBuffer
  {
    DirectX::XMFLOAT4   cameraPosition;
    DirectX::XMFLOAT4   lightPosition;
    DirectX::XMFLOAT4X4 viewProjection[2];
  };

  // Assert that the constant buffer remains 16-byte aligned (best practice).
  static_assert( ( sizeof( ViewProjectionConstantBuffer ) % ( sizeof( float ) * 4 ) ) == 0, "View/projection constant buffer size must be 16-byte aligned (16 bytes is the length of four floats)." );

  struct ViewProjection
  {
    DirectX::XMFLOAT4X4   view[2];
    DirectX::XMFLOAT4X4   projection[2];
  };

  class DeviceResources;

  // Manages DirectX device resources that are specific to a holographic camera, such as the
  // back buffer, ViewProjection constant buffer, and viewport.
  class CameraResources
  {
  public:
    CameraResources( HolographicCamera^ holographicCamera );

    void CreateResourcesForBackBuffer( DX::DeviceResources* pDeviceResources, HolographicCameraRenderingParameters^ cameraParameters );
    void ReleaseResourcesForBackBuffer( DX::DeviceResources* pDeviceResources );

    bool UpdateViewProjectionBuffer( std::shared_ptr<DX::DeviceResources> deviceResources, HolographicCameraPose^ cameraPose, SpatialCoordinateSystem^ coordinateSystem, DX::ViewProjection& vp);
    bool AttachViewProjectionBuffer( std::shared_ptr<DX::DeviceResources> deviceResources );

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
    HolographicCamera^ GetHolographicCamera() const;

  private:
    // Direct3D rendering objects. Required for 3D.
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView>    m_d3dRenderTargetView;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>    m_d3dDepthStencilView;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>           m_d3dBackBuffer;

    // Device resource to store view and projection matrices.
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_viewProjectionConstantBuffer;

    // Direct3D rendering properties.
    DXGI_FORMAT                                       m_dxgiFormat;
    Windows::Foundation::Size                         m_d3dRenderTargetSize;
    D3D11_VIEWPORT                                    m_d3dViewport;

    // Indicates whether the camera supports stereoscopic rendering.
    bool                                              m_isStereo = true;

    // Indicates whether this camera has a pending frame.
    bool                                              m_framePending = false;

    // Pointer to the holographic camera these resources are for.
    HolographicCamera^                                m_holographicCamera = nullptr;
  };
}
