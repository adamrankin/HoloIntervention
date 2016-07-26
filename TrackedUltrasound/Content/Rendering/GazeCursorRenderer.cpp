#include "pch.h"
#include "GazeCursorRenderer.h"
#include "DirectXHelper.h"

// DirectXTK includes
#include "Model.h"

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  //----------------------------------------------------------------------------
  // Loads vertex and pixel shaders from files and instantiates the cube geometry.
  GazeCursorRenderer::GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
    : m_deviceResources( deviceResources )
  {
    CreateDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::Update( float gazeTargetPosition[3], float gazeTargetNormal[3] )
  {
    if ( !m_enableCursor )
    {
      // No need to update, cursor is not drawn
      return;
    }

    // Get the gaze direction relative to the given coordinate system.
    memcpy(m_gazeTargetPosition, gazeTargetPosition, sizeof(float) * 3);
    memcpy(m_gazeTargetNormal, gazeTargetNormal, sizeof(float) * 3);
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::Render()
  {
    // Loading is asynchronous. Resources must be created before drawing can occur.
    if ( !m_loadingComplete || !m_enableCursor )
    {
      return;
    }

    const auto context = m_deviceResources->GetD3DDeviceContext();

    // Each vertex is one instance of the VertexPositionColor struct.
    
    const UINT stride = sizeof( VertexPositionColor );
    const UINT offset = 0;
    context->IASetVertexBuffers(
      0,
      1,
      m_vertexBuffer.GetAddressOf(),
      &stride,
      &offset
    );
    context->IASetIndexBuffer(
      m_indexBuffer.Get(),
      DXGI_FORMAT_R16_UINT, // Each index is one 16-bit unsigned integer (short).
      0
    );
    context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    context->IASetInputLayout( m_inputLayout.Get() );

    // Attach the vertex shader.
    context->VSSetShader(
      m_vertexShader.Get(),
      nullptr,
      0
    );
    // Apply the model constant buffer to the vertex shader.
    context->VSSetConstantBuffers(
      0,
      1,
      m_modelConstantBuffer.GetAddressOf()
    );

    if ( !m_usingVprtShaders )
    {
      // On devices that do not support the D3D11_FEATURE_D3D11_OPTIONS3::
      // VPAndRTArrayIndexFromAnyShaderFeedingRasterizer optional feature,
      // a pass-through geometry shader is used to set the render target
      // array index.
      context->GSSetShader(
        m_geometryShader.Get(),
        nullptr,
        0
      );
    }

    // Attach the pixel shader.
    context->PSSetShader(
      m_pixelShader.Get(),
      nullptr,
      0
    );

    // Draw the objects.
    context->DrawIndexedInstanced(
      m_indexCount,   // Index count per instance.
      2,              // Instance count.
      0,              // Start index location.
      0,              // Base vertex location.
      0               // Start instance location.
    );
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::EnableCursor( bool enable )
  {
    m_enableCursor = enable;
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::ToggleCursor()
  {
    m_enableCursor = !m_enableCursor;
  }

  //----------------------------------------------------------------------------
  bool GazeCursorRenderer::IsCursorEnabled() const
  {
    return m_enableCursor;
  }

  //----------------------------------------------------------------------------
  Numerics::float3 GazeCursorRenderer::GetPosition() const
  {
    // TODO : calculate and store intersection point
    return Numerics::float3( 1, 1, 0 );
  }

  //----------------------------------------------------------------------------
  Numerics::float3 GazeCursorRenderer::GetNormal() const
  {
    return Numerics::float3( 0, 1, 0 );
  }

  //----------------------------------------------------------------------------
  Concurrency::task<void> GazeCursorRenderer::LoadShadersAsync()
  {
    // On devices that do support the D3D11_FEATURE_D3D11_OPTIONS3::
    // VPAndRTArrayIndexFromAnyShaderFeedingRasterizer optional feature
    // we can avoid using a pass-through geometry shader to set the render
    // target array index, thus avoiding any overhead that would be
    // incurred by setting the geometry shader stage.
    std::wstring vertexShaderFileName = m_usingVprtShaders ? L"ms-appx:///VprtVertexShader.cso" : L"ms-appx:///VertexShader.cso";

    // Load shaders asynchronously.
    task<std::vector<byte>> loadVSTask = DX::ReadDataAsync( vertexShaderFileName );
    task<std::vector<byte>> loadPSTask = DX::ReadDataAsync( L"ms-appx:///PixelShader.cso" );

    task<std::vector<byte>> loadGSTask;
    if ( !m_usingVprtShaders )
    {
      // Load the pass-through geometry shader.
      loadGSTask = DX::ReadDataAsync( L"ms-appx:///GeometryShader.cso" );
    }

    // After the vertex shader file is loaded, create the shader and input layout.
    task<void> createVSTask = loadVSTask.then( [this]( const std::vector<byte>& fileData )
    {
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateVertexShader(
          &fileData[0],
          fileData.size(),
          nullptr,
          &m_vertexShader
        )
      );

      static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
      {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
      };

      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateInputLayout(
          vertexDesc,
          ARRAYSIZE( vertexDesc ),
          &fileData[0],
          fileData.size(),
          &m_inputLayout
        )
      );
    } );

    // After the pixel shader file is loaded, create the shader and constant buffer.
    task<void> createPSTask = loadPSTask.then( [this]( const std::vector<byte>& fileData )
    {
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreatePixelShader(
          &fileData[0],
          fileData.size(),
          nullptr,
          &m_pixelShader
        )
      );

      const CD3D11_BUFFER_DESC constantBufferDesc( sizeof( ModelConstantBuffer ), D3D11_BIND_CONSTANT_BUFFER );
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &constantBufferDesc,
          nullptr,
          &m_modelConstantBuffer
        )
      );
    } );

    task<void> createGSTask;
    if ( !m_usingVprtShaders )
    {
      // After the pass-through geometry shader file is loaded, create the shader.
      createGSTask = loadGSTask.then( [this]( const std::vector<byte>& fileData )
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateGeometryShader(
            &fileData[0],
            fileData.size(),
            nullptr,
            &m_geometryShader
          )
        );
      } );
    }

    // Once all shaders are loaded, create the mesh.
    task<void> shaderTaskGroup = m_usingVprtShaders ? ( createPSTask && createVSTask ) : ( createPSTask && createVSTask && createGSTask );

    return shaderTaskGroup;
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::CreateDeviceDependentResources()
  {
    m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

    auto loadShadersTask = LoadShadersAsync();

    std::shared_ptr<std::vector<uint8_t>> meshDataVector = std::make_shared<std::vector<uint8_t>>();
    loadShadersTask.then([&]()
    {
      m_effectFactory = std::make_unique<DGSLEffectFactory>();
      Model::CreateFromCMO(m_deviceResources->GetD3DDevice(), L"Assets/Models/gaze_cursor.cmo", fxFactory);
    });
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::ReleaseDeviceDependentResources()
  {
    m_enableCursor = false;
    m_loadingComplete = false;
    m_usingVprtShaders = false;
    m_vertexShader.Reset();
    m_inputLayout.Reset();
    m_pixelShader.Reset();
    m_geometryShader.Reset();
    m_modelConstantBuffer.Reset();
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
  }
}