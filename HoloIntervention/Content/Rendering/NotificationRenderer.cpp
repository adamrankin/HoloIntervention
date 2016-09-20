/*====================================================================
Copyright(c) 2016 Adam Rankin


Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files(the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and / or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
====================================================================*/

// Local includes
#include "pch.h"
#include "Common.h"
#include "DirectXHelper.h"
#include "NotificationRenderer.h"

using namespace DirectX;

namespace HoloIntervention
{
  namespace Rendering
  {
    const uint32 NotificationRenderer::BLUR_TARGET_WIDTH_PIXEL = 256;
    const uint32 NotificationRenderer::OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL = 2048;

    //----------------------------------------------------------------------------
    NotificationRenderer::NotificationRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources ( deviceResources )
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    NotificationRenderer::~NotificationRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Update( NotificationConstantBuffer& buffer )
    {
      if ( !m_loadingComplete )
      {
        return;
      }

      // Update the model transform buffer for the hologram.
      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(
        m_modelConstantBuffer.Get(),
        0,
        nullptr,
        &buffer,
        0,
        0
      );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Render()
    {
      // Loading is asynchronous. Resources must be created before drawing can occur.
      if ( !m_loadingComplete )
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      // Each vertex is one instance of the VertexPositionColor struct.
      const UINT stride = sizeof( VertexPositionColorTex );
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
        // a pass-through geometry shader sets the render target ID.
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
      context->PSSetShaderResources(
        0,
        1,
        m_distanceFieldRenderer->GetTexture().GetAddressOf()
      );
      context->PSSetSamplers(
        0,
        1,
        m_quadTextureSamplerState.GetAddressOf()
      );

      context->OMSetBlendState(m_blendState.Get(), nullptr, 0xffffffff);

      // Draw the objects.
      context->DrawIndexedInstanced( m_indexCount, 2, 0, 0, 0 );

      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::RenderText(const std::wstring& message)
    {
      m_distanceFieldRenderer->ResetRenderCount();
      m_textRenderer->RenderTextOffscreen(message);
      m_distanceFieldRenderer->RenderDistanceField(m_textRenderer->GetTexture());
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::CreateDeviceDependentResources()
    {
      m_textRenderer = std::make_unique<TextRenderer>( m_deviceResources, OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL, OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL );
      m_distanceFieldRenderer = std::make_unique<DistanceFieldRenderer>( m_deviceResources, BLUR_TARGET_WIDTH_PIXEL, BLUR_TARGET_WIDTH_PIXEL );

      m_textRenderer->CreateDeviceDependentResources();
      m_distanceFieldRenderer->CreateDeviceDependentResources();

      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      // If the optional VPRT feature is supported by the graphics device, we
      // can avoid using geometry shaders to set the render target array index.
      std::wstring vertexShaderFileName = m_usingVprtShaders
                                          ? L"ms-appx:///NotificationVprtVertexShader.cso"
                                          : L"ms-appx:///NotificationVertexShader.cso";

      // Load shaders asynchronously.
      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync( vertexShaderFileName );
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync( L"ms-appx:///NotificationPixelShader.cso" );

      task<std::vector<byte>> loadGSTask;
      if ( !m_usingVprtShaders )
      {
        // Load the pass-through geometry shader.
        // position, color, texture, index
        loadGSTask = DX::ReadDataAsync( L"ms-appx:///PCTIGeometryShader.cso" );
      }

      // After the vertex shader file is loaded, create the shader and input layout.
      task<void> createVSTask = loadVSTask.then( [this]( const std::vector<byte>& fileData )
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateVertexShader(
            fileData.data(),
            fileData.size(),
            nullptr,
            &m_vertexShader
          )
        );

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3> vertexDesc =
        {
          {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          }
        };

        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateInputLayout(
            vertexDesc.data(),
            vertexDesc.size(),
            fileData.data(),
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
            fileData.data(),
            fileData.size(),
            nullptr,
            &m_pixelShader
          )
        );

        const CD3D11_BUFFER_DESC constantBufferDesc( sizeof( NotificationConstantBuffer ), D3D11_BIND_CONSTANT_BUFFER );
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
        // After the geometry shader file is loaded, create the shader.
        createGSTask = loadGSTask.then( [this]( const std::vector<byte>& fileData )
        {
          DX::ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateGeometryShader(
              fileData.data(),
              fileData.size(),
              nullptr,
              &m_geometryShader
            )
          );
        } );
      }

      // Once all shaders are loaded, create the mesh.
      task<void> shaderTaskGroup = m_usingVprtShaders ? ( createPSTask && createVSTask ) : ( createPSTask && createVSTask && createGSTask );
      task<void> finishLoadingTask = shaderTaskGroup.then( [this]()
      {
        // Load mesh vertices. Each vertex has a position and a color.
        // Note that the quad size has changed from the default DirectX app
        // template. Windows Holographic is scaled in meters, so to draw the
        // quad at a comfortable size we made the quad width 0.2 m (20 cm).
        static const std::array<VertexPositionColorTex, 4> quadVertices =
        {
          {
            { XMFLOAT3( -0.2f,  0.2f, 0.f ), XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f ), XMFLOAT2( 0.f, 0.f ) },
            { XMFLOAT3( 0.2f,  0.2f, 0.f ), XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2( 1.f, 0.f ) },
            { XMFLOAT3( 0.2f, -0.2f, 0.f ), XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2( 1.f, 1.f ) },
            { XMFLOAT3( -0.2f, -0.2f, 0.f ), XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2( 0.f, 1.f ) },
          }
        };

        D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
        vertexBufferData.pSysMem = quadVertices.data();
        vertexBufferData.SysMemPitch = 0;
        vertexBufferData.SysMemSlicePitch = 0;
        const CD3D11_BUFFER_DESC vertexBufferDesc( sizeof( VertexPositionColorTex ) * quadVertices.size(), D3D11_BIND_VERTEX_BUFFER );
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBuffer(
            &vertexBufferDesc,
            &vertexBufferData,
            &m_vertexBuffer
          )
        );

        constexpr std::array<unsigned short, 12> quadIndices =
        {
          {
            // -z
            0, 2, 3,
            0, 1, 2,

            // +z
            2, 0, 3,
            1, 0, 2,
          }
        };

        m_indexCount = quadIndices.size();

        D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
        indexBufferData.pSysMem = quadIndices.data();
        indexBufferData.SysMemPitch = 0;
        indexBufferData.SysMemSlicePitch = 0;
        const CD3D11_BUFFER_DESC indexBufferDesc( sizeof( unsigned short ) * quadIndices.size(), D3D11_BIND_INDEX_BUFFER );
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBuffer(
            &indexBufferDesc,
            &indexBufferData,
            &m_indexBuffer
          )
        );

        D3D11_SAMPLER_DESC desc;
        ZeroMemory( &desc, sizeof( D3D11_SAMPLER_DESC ) );
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 3;
        desc.MinLOD = 0;
        desc.MaxLOD = 3;
        desc.MipLODBias = 0.f;
        desc.BorderColor[0] = 0.f;
        desc.BorderColor[1] = 0.f;
        desc.BorderColor[2] = 0.f;
        desc.BorderColor[3] = 0.f;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateSamplerState(
            &desc,
            &m_quadTextureSamplerState
          )
        );

        D3D11_BLEND_DESC blendDesc;
        ZeroMemory(&blendDesc, sizeof(D3D11_BLEND_DESC));
        blendDesc.AlphaToCoverageEnable = FALSE;
        blendDesc.IndependentBlendEnable = FALSE;
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_MAX;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_DEST_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBlendState(
            &blendDesc,
            &m_blendState
          )
        );

        // After the assets are loaded, the quad is ready to be rendered.
        m_loadingComplete = true;
      } );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::ReleaseDeviceDependentResources()
    {
      m_loadingComplete = false;
      m_usingVprtShaders = false;

      m_textRenderer = nullptr;
      m_distanceFieldRenderer = nullptr;

      m_vertexShader.Reset();
      m_inputLayout.Reset();
      m_pixelShader.Reset();
      m_geometryShader.Reset();

      m_modelConstantBuffer.Reset();

      m_vertexBuffer.Reset();
      m_indexBuffer.Reset();

      m_quadTextureSamplerState.Reset();
    }
  }
}