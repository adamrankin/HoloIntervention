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

// Local includes
#include "pch.h"
#include "CameraResources.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"

// WinRT includes
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;

namespace DX
{
  //----------------------------------------------------------------------------
  CameraResources::CameraResources(HolographicCamera^ camera)
    : m_holographicCamera(camera)
    , m_isStereo(camera->IsStereo)
    , m_d3dRenderTargetSize(camera->RenderTargetSize)
  {
    m_d3dViewport = CD3D11_VIEWPORT(0.f, 0.f, m_d3dRenderTargetSize.Width, m_d3dRenderTargetSize.Height);
  };

  //----------------------------------------------------------------------------
  void CameraResources::CreateResourcesForBackBuffer(
    DX::DeviceResources* pDeviceResources,
    HolographicCameraRenderingParameters^ cameraParameters
  )
  {
    const auto device = pDeviceResources->GetD3DDevice();

    IDirect3DSurface^ surface = cameraParameters->Direct3D11BackBuffer;

    ComPtr<ID3D11Resource> resource;
    DX::ThrowIfFailed(GetDXGIInterfaceFromObject(surface, IID_PPV_ARGS(&resource)));

    ComPtr<ID3D11Texture2D> cameraBackBuffer;
    DX::ThrowIfFailed(resource.As(&cameraBackBuffer));

    if (m_d3dBackBuffer.Get() != cameraBackBuffer.Get())
    {
      m_d3dBackBuffer = cameraBackBuffer;

      DX::ThrowIfFailed(device->CreateRenderTargetView(m_d3dBackBuffer.Get(), nullptr, &m_d3dRenderTargetView));

      D3D11_TEXTURE2D_DESC backBufferDesc;
      m_d3dBackBuffer->GetDesc(&backBufferDesc);
      m_dxgiFormat = backBufferDesc.Format;

      Windows::Foundation::Size currentSize = m_holographicCamera->RenderTargetSize;
      if (m_d3dRenderTargetSize != currentSize)
      {
        m_d3dRenderTargetSize = currentSize;
        m_d3dDepthStencilView.Reset();
      }
    }

    // Refresh depth stencil resources, if needed.
    if (m_d3dDepthStencilView == nullptr)
    {
      CD3D11_TEXTURE2D_DESC depthStencilDesc(DXGI_FORMAT_D16_UNORM, static_cast<UINT>(m_d3dRenderTargetSize.Width), static_cast<UINT>(m_d3dRenderTargetSize.Height),
                                             m_isStereo ? 2 : 1, // Create two textures when rendering in stereo.
                                             1, // Use a single mipmap level.
                                             D3D11_BIND_DEPTH_STENCIL
                                            );

      ComPtr<ID3D11Texture2D> depthStencil;
      DX::ThrowIfFailed(device->CreateTexture2D(&depthStencilDesc, nullptr, &depthStencil));

      CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(m_isStereo ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D);
      DX::ThrowIfFailed(device->CreateDepthStencilView(depthStencil.Get(), &depthStencilViewDesc, &m_d3dDepthStencilView));
    }

    // Create the constant buffer, if needed.
    if (m_viewProjectionConstantBuffer == nullptr)
    {
      // Create a constant buffer to store view and projection matrices for the camera.
      CD3D11_BUFFER_DESC constantBufferDesc(sizeof(ViewProjectionConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
      DX::ThrowIfFailed(device->CreateBuffer(&constantBufferDesc, nullptr, &m_viewProjectionConstantBuffer));
    }
  }

  //----------------------------------------------------------------------------
  void CameraResources::ReleaseResourcesForBackBuffer(DX::DeviceResources* pDeviceResources)
  {
    const auto context = pDeviceResources->GetD3DDeviceContext();

    // Release camera-specific resources.
    m_d3dBackBuffer.Reset();
    m_d3dRenderTargetView.Reset();
    m_d3dDepthStencilView.Reset();
    m_viewProjectionConstantBuffer.Reset();

    // Ensure system references to the back buffer are released by clearing the render
    // target from the graphics pipeline state, and then flushing the Direct3D context.
    ID3D11RenderTargetView* nullViews[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { nullptr };
    context->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
    context->Flush();
  }

  //----------------------------------------------------------------------------
  bool CameraResources::UpdateViewProjectionBuffer(
    std::shared_ptr<DX::DeviceResources> deviceResources,
    HolographicCameraPose^ cameraPose,
    SpatialCoordinateSystem^ coordinateSystem,
    DX::ViewProjection& vp
  )
  {
    // The system changes the viewport on a per-frame basis for system optimizations.
    m_d3dViewport = CD3D11_VIEWPORT(cameraPose->Viewport.Left, cameraPose->Viewport.Top, cameraPose->Viewport.Width, cameraPose->Viewport.Height);

    // The projection transform for each frame is provided by the HolographicCameraPose.
    HolographicStereoTransform cameraProjectionTransform = cameraPose->ProjectionTransform;

    // Get a container object with the view and projection matrices for the given
    // pose in the given coordinate system.
    Platform::IBox<HolographicStereoTransform>^ viewTransformContainer = cameraPose->TryGetViewTransform(coordinateSystem);

    // If TryGetViewTransform returns a null pointer, that means the pose and coordinate
    // system cannot be understood relative to one another; content cannot be rendered
    // in this coordinate system for the duration of the current frame.
    // This usually means that positional tracking is not active for the current frame, in
    // which case it is possible to use a SpatialLocatorAttachedFrameOfReference to render
    // content that is not world-locked instead.
    ViewProjectionConstantBuffer viewProjectionConstantBufferData;
    bool viewTransformAcquired = viewTransformContainer != nullptr;
    if (viewTransformAcquired)
    {
      // Otherwise, the set of view transforms can be retrieved.
      HolographicStereoTransform viewCoordinateSystemTransform = viewTransformContainer->Value;

      XMStoreFloat4x4(&vp.view[0], XMLoadFloat4x4(&viewCoordinateSystemTransform.Left));
      XMStoreFloat4x4(&vp.view[1], XMLoadFloat4x4(&viewCoordinateSystemTransform.Right));

      XMStoreFloat4x4(&vp.projection[0], XMLoadFloat4x4(&cameraProjectionTransform.Left));
      XMStoreFloat4x4(&vp.projection[1], XMLoadFloat4x4(&cameraProjectionTransform.Right));

      // Update the view matrices. Holographic cameras (such as Microsoft HoloLens) are
      // constantly moving relative to the world. The view matrices need to be updated
      // every frame.
      XMStoreFloat4x4(
        &viewProjectionConstantBufferData.viewProjection[0],
        XMMatrixTranspose(XMLoadFloat4x4(&viewCoordinateSystemTransform.Left) * XMLoadFloat4x4(&cameraProjectionTransform.Left))
      );
      XMStoreFloat4x4(
        &viewProjectionConstantBufferData.viewProjection[1],
        XMMatrixTranspose(XMLoadFloat4x4(&viewCoordinateSystemTransform.Right) * XMLoadFloat4x4(&cameraProjectionTransform.Right))
      );

      float4x4 viewInverse;
      bool invertible = Windows::Foundation::Numerics::invert(viewCoordinateSystemTransform.Left, &viewInverse);
      if (invertible)
      {
        // For the purposes of this app, use the camera position as a light source.
        float4 cameraPosition = float4(viewInverse.m41, viewInverse.m42, viewInverse.m43, 0.f);
        float4 lightPosition = cameraPosition + float4(0.f, 0.25f, 0.f, 0.f);

        XMStoreFloat4(&viewProjectionConstantBufferData.cameraPosition, DirectX::XMLoadFloat4(&cameraPosition));
        XMStoreFloat4(&viewProjectionConstantBufferData.lightPosition, DirectX::XMLoadFloat4(&lightPosition));
      }
    }

    // Use the D3D device context to update Direct3D device-based resources.
    const auto context = deviceResources->GetD3DDeviceContext();

    // Loading is asynchronous. Resources must be created before they can be updated.
    if (context == nullptr || m_viewProjectionConstantBuffer == nullptr || !viewTransformAcquired)
    {
      m_framePending = false;
      return false;
    }
    else
    {
      // Update the view and projection matrices.
      context->UpdateSubresource(
        m_viewProjectionConstantBuffer.Get(),
        0,
        nullptr,
        &viewProjectionConstantBufferData,
        0,
        0
      );

      m_framePending = true;
      return true;
    }
  }

  //----------------------------------------------------------------------------
  bool CameraResources::AttachViewProjectionBuffer(
    std::shared_ptr<DX::DeviceResources> deviceResources
  )
  {
    // This method uses Direct3D device-based resources.
    const auto context = deviceResources->GetD3DDeviceContext();

    // Loading is asynchronous. Resources must be created before they can be updated.
    // Cameras can also be added asynchronously, in which case they must be initialized
    // before they can be used.
    if (context == nullptr || m_viewProjectionConstantBuffer == nullptr || m_framePending == false)
    {
      return false;
    }

    // Set the viewport for this camera.
    context->RSSetViewports(1, &m_d3dViewport);

    // Send the constant buffer to the vertex shader.
    context->VSSetConstantBuffers(
      1,
      1,
      m_viewProjectionConstantBuffer.GetAddressOf()
    );

    // Send the constant buffer to the pixel shader.
    context->PSSetConstantBuffers(
      1,
      1,
      m_viewProjectionConstantBuffer.GetAddressOf()
    );

    m_framePending = false;

    return true;
  }

  //----------------------------------------------------------------------------
  ID3D11RenderTargetView* CameraResources::GetBackBufferRenderTargetView() const
  {
    return m_d3dRenderTargetView.Get();
  }

  //----------------------------------------------------------------------------
  ID3D11DepthStencilView* CameraResources::GetDepthStencilView() const
  {
    return m_d3dDepthStencilView.Get();
  }

  //----------------------------------------------------------------------------
  ID3D11Texture2D* CameraResources::GetBackBufferTexture2D() const
  {
    return m_d3dBackBuffer.Get();
  }

  //----------------------------------------------------------------------------
  D3D11_VIEWPORT CameraResources::GetViewport() const
  {
    return m_d3dViewport;
  }

  //----------------------------------------------------------------------------
  DXGI_FORMAT CameraResources::GetBackBufferDXGIFormat() const
  {
    return m_dxgiFormat;
  }

  //----------------------------------------------------------------------------
  Windows::Foundation::Size CameraResources::GetRenderTargetSize() const
  {
    return m_d3dRenderTargetSize;
  }

  //----------------------------------------------------------------------------
  bool CameraResources::IsRenderingStereoscopic() const
  {
    return m_isStereo;
  }

  //----------------------------------------------------------------------------
  Windows::Graphics::Holographic::HolographicCamera^ CameraResources::GetHolographicCamera() const
  {
    return m_holographicCamera;
  }
}