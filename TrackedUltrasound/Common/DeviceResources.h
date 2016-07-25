﻿
#pragma once

#include "CameraResources.h"

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p)=nullptr; } }
#endif

namespace DX
{
  // Provides an interface for an application that owns DeviceResources to be notified of the device being lost or created.
  interface IDeviceNotify
  {
    virtual void OnDeviceLost() = 0;
    virtual void OnDeviceRestored() = 0;
  };

  // Creates and manages a Direct3D device and immediate context, Direct2D device and context (for debug), and the holographic swap chain.
  class DeviceResources
  {
  public:
    DeviceResources();

    // Public methods related to Direct3D devices.
    void HandleDeviceLost();
    void RegisterDeviceNotify( IDeviceNotify* deviceNotify );
    void Trim();
    void Present( Windows::Graphics::Holographic::HolographicFrame^ frame );

    // Public methods related to holographic devices.
    void SetHolographicSpace( Windows::Graphics::Holographic::HolographicSpace^ space );
    void EnsureCameraResources( Windows::Graphics::Holographic::HolographicFrame^ frame,
                                Windows::Graphics::Holographic::HolographicFramePrediction^ prediction );

    void AddHolographicCamera( Windows::Graphics::Holographic::HolographicCamera^ camera );
    void RemoveHolographicCamera( Windows::Graphics::Holographic::HolographicCamera^ camera );

    // Holographic accessors.
    template<typename RetType, typename LCallback>
    RetType UseHolographicCameraResources( const LCallback& callback );

    Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice^ GetD3DInteropDevice() const;

    // D3D accessors.
    ID3D11Device4* GetD3DDevice() const;
    ID3D11DeviceContext3* GetD3DDeviceContext() const;
    D3D_FEATURE_LEVEL GetDeviceFeatureLevel() const;
    bool GetDeviceSupportsVprt() const;

    // DXGI accessors.
    IDXGIAdapter3* GetDXGIAdapter() const;

    // D2D accessors.
    ID2D1Factory2* GetD2DFactory() const;
    IDWriteFactory2* GetDWriteFactory() const;
    IWICImagingFactory2* GetWicImagingFactory() const;

  private:
    // Private methods related to the Direct3D device, and resources based on that device.
    void CreateDeviceIndependentResources();
    void InitializeUsingHolographicSpace();
    void CreateDeviceResources();

    // Direct3D objects.
    Microsoft::WRL::ComPtr<ID3D11Device4> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext3> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGIAdapter3> m_dxgiAdapter;

    // Direct3D interop objects.
    Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice^ m_d3dInteropDevice;

    // Direct2D factories.
    Microsoft::WRL::ComPtr<ID2D1Factory2> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory2> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IWICImagingFactory2> m_wicFactory;

    // The holographic space provides a preferred DXGI adapter ID.
    Windows::Graphics::Holographic::HolographicSpace^ m_holographicSpace = nullptr;

    // Properties of the Direct3D device currently in use.
    D3D_FEATURE_LEVEL m_d3dFeatureLevel = D3D_FEATURE_LEVEL_10_0;

    // The IDeviceNotify can be held directly as it owns the DeviceResources.
    IDeviceNotify* m_deviceNotify = nullptr;

    // Whether or not the current Direct3D device supports the optional feature
    // for setting the render target array index from the vertex shader stage.
    bool m_supportsVprt = false;

    // Back buffer resources, etc. for attached holographic cameras.
    std::map<UINT32, std::unique_ptr<CameraResources>> m_cameraResources;
    std::mutex m_cameraResourcesLock;
  };

  // Device-based resources for holographic cameras are stored in a std::map. Access this list by providing a
  // callback to this function, and the std::map will be guarded from add and remove
  // events until the callback returns. The callback is processed immediately and must
  // not contain any nested calls to UseHolographicCameraResources.
  // The callback takes a parameter of type std::map<UINT32, std::unique_ptr<DX::CameraResources>>&
  // through which the list of cameras will be accessed.

  //----------------------------------------------------------------------------
  template<typename RetType, typename LCallback>
  RetType DeviceResources::UseHolographicCameraResources( const LCallback& callback )
  {
    std::lock_guard<std::mutex> guard( m_cameraResourcesLock );
    return callback( m_cameraResources );
  }

}