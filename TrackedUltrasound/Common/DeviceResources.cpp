﻿
#include "pch.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"

#include <Collection.h>
#include <windows.graphics.directx.direct3d11.interop.h>

using namespace D2D1;
using namespace Microsoft::WRL;
using namespace Windows::Graphics::DirectX::Direct3D11;
using namespace Windows::Graphics::Display;
using namespace Windows::Graphics::Holographic;

namespace DX
{
  //----------------------------------------------------------------------------
  // Method:    DeviceResources
  // FullName:  DX::DeviceResources::DeviceResources
  // Access:    public
  // Returns:
  // Qualifier:
  DeviceResources::DeviceResources()
  {
    CreateDeviceIndependentResources();
  }

  //----------------------------------------------------------------------------
  // Method:    CreateDeviceIndependentResources
  // FullName:  DX::DeviceResources::CreateDeviceIndependentResources
  // Access:    private
  // Returns:   void
  // Qualifier:
  //
  // Configures resources that don't depend on the Direct3D device.
  void DeviceResources::CreateDeviceIndependentResources()
  {
    // Initialize Direct2D resources.
    D2D1_FACTORY_OPTIONS options{};

#if defined(_DEBUG)
    // If the project is in a debug build, enable Direct2D debugging via SDK Layers.
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    // Initialize the Direct2D Factory.
    ThrowIfFailed(
      D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof( ID2D1Factory2 ),
        &options,
        &m_d2dFactory
      )
    );

    // Initialize the DirectWrite Factory.
    ThrowIfFailed(
      DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof( IDWriteFactory2 ),
        &m_dwriteFactory
      )
    );

    // Initialize the Windows Imaging Component (WIC) Factory.
    ThrowIfFailed(
      CoCreateInstance(
        CLSID_WICImagingFactory2,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS( &m_wicFactory )
      )
    );
  }

  //----------------------------------------------------------------------------
  // Method:    SetHolographicSpace
  // FullName:  DX::DeviceResources::SetHolographicSpace
  // Access:    public
  // Returns:   void
  // Qualifier:
  // Parameter: HolographicSpace ^ holographicSpace
  void DeviceResources::SetHolographicSpace( HolographicSpace^ holographicSpace )
  {
    // Cache the holographic space. Used to re-initialize during device-lost scenarios.
    m_holographicSpace = holographicSpace;

    InitializeUsingHolographicSpace();
  }

  //----------------------------------------------------------------------------
  // Method:    InitializeUsingHolographicSpace
  // FullName:  DX::DeviceResources::InitializeUsingHolographicSpace
  // Access:    private
  // Returns:   void
  // Qualifier:
  void DeviceResources::InitializeUsingHolographicSpace()
  {
    // The holographic space might need to determine which adapter supports
    // holograms, in which case it will specify a non-zero PrimaryAdapterId.
    LUID id =
    {
      m_holographicSpace->PrimaryAdapterId.LowPart,
      m_holographicSpace->PrimaryAdapterId.HighPart
    };

    // When a primary adapter ID is given to the app, the app should find
    // the corresponding DXGI adapter and use it to create Direct3D devices
    // and device contexts. Otherwise, there is no restriction on the DXGI
    // adapter the app can use.
    if ( ( id.HighPart != 0 ) && ( id.LowPart != 0 ) )
    {
      UINT createFlags = 0;
#ifdef _DEBUG
      if ( SdkLayersAvailable() )
      {
        createFlags |= DXGI_CREATE_FACTORY_DEBUG;
      }
#endif
      // Create the DXGI factory.
      ComPtr<IDXGIFactory1> dxgiFactory;
      ThrowIfFailed(
        CreateDXGIFactory2(
          createFlags,
          IID_PPV_ARGS( &dxgiFactory )
        )
      );
      ComPtr<IDXGIFactory4> dxgiFactory4;
      ThrowIfFailed( dxgiFactory.As( &dxgiFactory4 ) );

      // Retrieve the adapter specified by the holographic space.
      ThrowIfFailed(
        dxgiFactory4->EnumAdapterByLuid(
          id,
          IID_PPV_ARGS( &m_dxgiAdapter )
        )
      );
    }
    else
    {
      m_dxgiAdapter.Reset();
    }

    try
    {
      CreateDeviceResources();
    }
    catch (const std::exception& e)
    {
      OutputDebugStringA(e.what());
      return;
    }

    m_holographicSpace->SetDirect3D11Device( m_d3dInteropDevice );
  }

  //----------------------------------------------------------------------------
  // Method:    CreateDeviceResources
  // FullName:  DX::DeviceResources::CreateDeviceResources
  // Access:    private
  // Returns:   void
  // Qualifier:
  //
  // Configures the Direct3D device, and stores handles to it and the device context.
  void DeviceResources::CreateDeviceResources()
  {
    // This flag adds support for surfaces with a different color channel ordering
    // than the API default. It is required for compatibility with Direct2D.
    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#if defined(_DEBUG)
    if ( SdkLayersAvailable() )
    {
      // If the project is in a debug build, enable debugging via SDK Layers with this flag.
      creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }
#endif

    // This array defines the set of DirectX hardware feature levels this app will support.
    // Note the ordering should be preserved.
    // Note that HoloLens supports feature level 11.1. The HoloLens emulator is also capable
    // of running on graphics cards starting with feature level 10.0.
    D3D_FEATURE_LEVEL featureLevels[] =
    {
      D3D_FEATURE_LEVEL_12_1,
      D3D_FEATURE_LEVEL_12_0,
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0
    };

    // Create the Direct3D 11 API device object and a corresponding context.
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    const HRESULT hr = D3D11CreateDevice(
                         m_dxgiAdapter.Get(),        // Either nullptr, or the primary adapter determined by Windows Holographic.
                         D3D_DRIVER_TYPE_HARDWARE,   // Create a device using the hardware graphics driver.
                         0,                          // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
                         creationFlags,              // Set debug and Direct2D compatibility flags.
                         featureLevels,              // List of feature levels this app can support.
                         ARRAYSIZE( featureLevels ), // Size of the list above.
                         D3D11_SDK_VERSION,          // Always set this to D3D11_SDK_VERSION for Windows Store apps.
                         &device,                    // Returns the Direct3D device created.
                         &m_d3dFeatureLevel,         // Returns feature level of device created.
                         &context                    // Returns the device immediate context.
                       );

    if ( FAILED( hr ) )
    {
      // If the initialization fails, fall back to the WARP device.
      // For more information on WARP, see:
      // http://go.microsoft.com/fwlink/?LinkId=286690
      ThrowIfFailed(
        D3D11CreateDevice(
          nullptr,              // Use the default DXGI adapter for WARP.
          D3D_DRIVER_TYPE_WARP, // Create a WARP device instead of a hardware device.
          0,
          creationFlags,
          featureLevels,
          ARRAYSIZE( featureLevels ),
          D3D11_SDK_VERSION,
          &device,
          &m_d3dFeatureLevel,
          &context
        )
      );
    }

    // Store pointers to the Direct3D device and immediate context.
    ThrowIfFailed(
      device.As( &m_d3dDevice )
    );

    ThrowIfFailed(
      context.As( &m_d3dContext )
    );

    // Acquire the DXGI interface for the Direct3D device.
    ComPtr<IDXGIDevice3> dxgiDevice;
    ThrowIfFailed(
      m_d3dDevice.As( &dxgiDevice )
    );

    // Wrap the native device using a WinRT interop object.
    m_d3dInteropDevice = CreateDirect3DDevice( dxgiDevice.Get() );

    // Cache the DXGI adapter.
    // This is for the case of no preferred DXGI adapter, or fallback to WARP.
    ComPtr<IDXGIAdapter> dxgiAdapter;
    ThrowIfFailed(
      dxgiDevice->GetAdapter( &dxgiAdapter )
    );
    ThrowIfFailed(
      dxgiAdapter.As( &m_dxgiAdapter )
    );

    // Check for device support for the optional feature that allows setting the render target array index from the vertex shader stage.
    D3D11_FEATURE_DATA_D3D11_OPTIONS3 options;
    m_d3dDevice->CheckFeatureSupport( D3D11_FEATURE_D3D11_OPTIONS3, &options, sizeof( options ) );
    if ( options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer )
    {
      m_supportsVprt = true;
    }
    D3D11_FEATURE_DATA_DOUBLES hwopts;
    m_d3dDevice->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &hwopts, sizeof(hwopts));
    if (!hwopts.DoublePrecisionFloatShaderOps)
    {
      throw std::exception("No hardware double-precision capable device found. Cannot create D3D device!");
    }
  }

  // Validates the back buffer for each HolographicCamera and recreates
  // resources for back buffers that have changed.
  // Locks the set of holographic camera resources until the function exits.
  void DeviceResources::EnsureCameraResources(
    HolographicFrame^ frame,
    HolographicFramePrediction^ prediction )
  {
    UseHolographicCameraResources<void>( [this, frame, prediction]( std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap )
    {
      for ( HolographicCameraPose^ pose : prediction->CameraPoses )
      {
        HolographicCameraRenderingParameters^ renderingParameters = frame->GetRenderingParameters( pose );
        CameraResources* pCameraResources = cameraResourceMap[pose->HolographicCamera->Id].get();

        pCameraResources->CreateResourcesForBackBuffer( this, renderingParameters );
      }
    } );
  }

  // Prepares to allocate resources and adds resource views for a camera.
  // Locks the set of holographic camera resources until the function exits.
  void DeviceResources::AddHolographicCamera( HolographicCamera^ camera )
  {
    UseHolographicCameraResources<void>( [this, camera]( std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap )
    {
      cameraResourceMap[camera->Id] = std::make_unique<CameraResources>( camera );
    } );
  }

  // Deallocates resources for a camera and removes the camera from the set.
  // Locks the set of holographic camera resources until the function exits.
  void DeviceResources::RemoveHolographicCamera( HolographicCamera^ camera )
  {
    UseHolographicCameraResources<void>( [this, camera]( std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap )
    {
      CameraResources* pCameraResources = cameraResourceMap[camera->Id].get();

      if ( pCameraResources != nullptr )
      {
        pCameraResources->ReleaseResourcesForBackBuffer( this );
        cameraResourceMap.erase( camera->Id );
      }
    } );
  }

  // Recreate all device resources and set them back to the current state.
  // Locks the set of holographic camera resources until the function exits.
  void DeviceResources::HandleDeviceLost()
  {
    if ( m_deviceNotify != nullptr )
    {
      m_deviceNotify->OnDeviceLost();
    }

    UseHolographicCameraResources<void>( [this]( std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap )
    {
      for ( auto& pair : cameraResourceMap )
      {
        CameraResources* pCameraResources = pair.second.get();
        pCameraResources->ReleaseResourcesForBackBuffer( this );
      }
    } );

    InitializeUsingHolographicSpace();

    if ( m_deviceNotify != nullptr )
    {
      m_deviceNotify->OnDeviceRestored();
    }
  }

  // Register our DeviceNotify to be informed on device lost and creation.
  void DeviceResources::RegisterDeviceNotify( IDeviceNotify* deviceNotify )
  {
    m_deviceNotify = deviceNotify;
  }

  // Call this method when the app suspends. It provides a hint to the driver that the app
  // is entering an idle state and that temporary buffers can be reclaimed for use by other apps.
  void DeviceResources::Trim()
  {
    m_d3dContext->ClearState();

    ComPtr<IDXGIDevice3> dxgiDevice;
    ThrowIfFailed( m_d3dDevice.As( &dxgiDevice ) );
    dxgiDevice->Trim();
  }

  // Present the contents of the swap chain to the screen.
  // Locks the set of holographic camera resources until the function exits.
  void DeviceResources::Present( HolographicFrame^ frame )
  {
    // By default, this API waits for the frame to finish before it returns.
    // Holographic apps should wait for the previous frame to finish before
    // starting work on a new frame. This allows for better results from
    // holographic frame predictions.
    HolographicFramePresentResult presentResult = frame->PresentUsingCurrentPrediction();

    HolographicFramePrediction^ prediction = frame->CurrentPrediction;
    UseHolographicCameraResources<void>( [this, prediction]( std::map<UINT32, std::unique_ptr<CameraResources>>& cameraResourceMap )
    {
      for ( auto cameraPose : prediction->CameraPoses )
      {
        // This represents the device-based resources for a HolographicCamera.
        CameraResources* pCameraResources = cameraResourceMap[cameraPose->HolographicCamera->Id].get();

        // Discard the contents of the render target.
        // This is a valid operation only when the existing contents will be
        // entirely overwritten. If dirty or scroll rects are used, this call
        // should be removed.
        m_d3dContext->DiscardView( pCameraResources->GetBackBufferRenderTargetView() );

        // Discard the contents of the depth stencil.
        m_d3dContext->DiscardView( pCameraResources->GetDepthStencilView() );
      }
    } );

    // The PresentUsingCurrentPrediction API will detect when the graphics device
    // changes or becomes invalid. When this happens, it is considered a Direct3D
    // device lost scenario.
    if ( presentResult == HolographicFramePresentResult::DeviceRemoved )
    {
      // The Direct3D device, context, and resources should be recreated.
      HandleDeviceLost();
    }
  }

  //----------------------------------------------------------------------------
  Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice^ DeviceResources::GetD3DInteropDevice() const
  {
    return m_d3dInteropDevice;
  }

  //----------------------------------------------------------------------------
  ID3D11Device4* DeviceResources::GetD3DDevice() const
  {
    return m_d3dDevice.Get();
  }

  //----------------------------------------------------------------------------
  ID3D11DeviceContext3* DeviceResources::GetD3DDeviceContext() const
  {
    return m_d3dContext.Get();
  }

  //----------------------------------------------------------------------------
  D3D_FEATURE_LEVEL DeviceResources::GetDeviceFeatureLevel() const
  {
    return m_d3dFeatureLevel;
  }

  //----------------------------------------------------------------------------
  bool DeviceResources::GetDeviceSupportsVprt() const
  {
    return m_supportsVprt;
  }

  //----------------------------------------------------------------------------
  IDXGIAdapter3* DeviceResources::GetDXGIAdapter() const
  {
    return m_dxgiAdapter.Get();
  }

  //----------------------------------------------------------------------------
  ID2D1Factory2* DeviceResources::GetD2DFactory() const
  {
    return m_d2dFactory.Get();
  }

  //----------------------------------------------------------------------------
  IDWriteFactory2* DeviceResources::GetDWriteFactory() const
  {
    return m_dwriteFactory.Get();
  }

  //----------------------------------------------------------------------------
  IWICImagingFactory2* DeviceResources::GetWicImagingFactory() const
  {
    return m_wicFactory.Get();
  }

}