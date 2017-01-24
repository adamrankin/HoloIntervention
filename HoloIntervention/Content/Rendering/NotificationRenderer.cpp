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
#include "DistanceFieldRenderer.h"
#include "NotificationRenderer.h"
#include "TextRenderer.h"

// Common includes
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Rendering
  {
    const uint32 NotificationRenderer::BLUR_TARGET_WIDTH_PIXEL = 256;
    const uint32 NotificationRenderer::OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL = 2048;

    //----------------------------------------------------------------------------
    NotificationRenderer::NotificationRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      CreateDeviceDependentResourcesAsync();
    }

    //----------------------------------------------------------------------------
    NotificationRenderer::~NotificationRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Update(const float4x4& worldMatrix, const float4& hologramColorFadeMultiplier)
    {
      if (!m_componentReady)
      {
        return;
      }

      XMStoreFloat4x4(&m_constantBufferData.worldMatrix, XMLoadFloat4x4(&worldMatrix));
      XMStoreFloat4(&m_constantBufferData.hologramColorFadeMultiplier, XMLoadFloat4(&hologramColorFadeMultiplier));

      // Update the model transform buffer for the hologram.
      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &m_constantBufferData, 0, 0);
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Render()
    {
      // Loading is asynchronous. Resources must be created before drawing can occur.
      if (!m_componentReady)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      // Each vertex is one instance of the VertexPositionColor struct.
      const UINT stride = sizeof(VertexPositionColorTex);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      // Attach the vertex shader.
      context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      // Apply the model constant buffer to the vertex shader.
      context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_geometryShader.Get(), nullptr, 0);
      }

      // Attach the pixel shader.
      context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      context->PSSetShaderResources(0, 1, m_distanceFieldRenderer->GetTexture().GetAddressOf());
      context->PSSetSamplers(0, 1, m_quadTextureSamplerState.GetAddressOf());

      context->OMSetBlendState(m_blendState.Get(), nullptr, 0xffffffff);

      // Draw the objects.
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);

      ID3D11SamplerState* ppNullptr[1] = { nullptr };
      context->PSSetSamplers(0, 1, ppNullptr);
      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
      ID3D11ShaderResourceView* ppSRVnullptr[1] = { nullptr };
      context->PSSetShaderResources(0, 1, ppSRVnullptr);
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::RenderText(const std::wstring& message)
    {
      m_distanceFieldRenderer->ResetRenderCount();
      m_textRenderer->RenderTextOffscreen(message);
      m_distanceFieldRenderer->RenderDistanceField(m_textRenderer->GetTexture());
    }

    //----------------------------------------------------------------------------
    task<void> NotificationRenderer::CreateDeviceDependentResourcesAsync()
    {
      if (m_componentReady)
      {
        return create_task([]() {});
      }

      m_textRenderer = std::make_unique<TextRenderer>(m_deviceResources, OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL, OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL);
      m_textRenderer->CreateDeviceDependentResources();
      m_distanceFieldRenderer = std::make_unique<DistanceFieldRenderer>(m_deviceResources, BLUR_TARGET_WIDTH_PIXEL, BLUR_TARGET_WIDTH_PIXEL);
      m_distanceFieldRenderer->CreateDeviceDependentResources();

      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      std::wstring vertexShaderFileName = m_usingVprtShaders
                                          ? L"ms-appx:///NotificationVprtVertexShader.cso"
                                          : L"ms-appx:///NotificationVertexShader.cso";

      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(vertexShaderFileName);
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///NotificationPixelShader.cso");
      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PCTIGeometryShader.cso");
      }

      task<void> createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateVertexShader(fileData.data(), fileData.size(), nullptr, &m_vertexShader));

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3> vertexDesc =
        {
          {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          }
        };

        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateInputLayout(vertexDesc.data(), vertexDesc.size(), fileData.data(), fileData.size(), &m_inputLayout));
      });

      task<void> createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreatePixelShader(fileData.data(), fileData.size(), nullptr, &m_pixelShader));

        const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(NotificationConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&constantBufferDesc, nullptr, &m_constantBuffer));
      });

      task<void> createGSTask;
      if (!m_usingVprtShaders)
      {
        createGSTask = loadGSTask.then([this](const std::vector<byte>& fileData)
        {
          DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateGeometryShader(fileData.data(), fileData.size(), nullptr, &m_geometryShader));
        });
      }

      task<void> shaderTaskGroup = m_usingVprtShaders ? (createPSTask && createVSTask) : (createPSTask && createVSTask && createGSTask);
      return shaderTaskGroup.then([this]()
      {
        // Windows Holographic is scaled in meters, so to draw the
        // quad at a comfortable size we made the quad width 0.2 m (20 cm).
        static const std::array<VertexPositionColorTex, 4> quadVertices =
        {
          {
            { XMFLOAT3(-0.2f,  0.2f, 0.f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2(0.f, 0.f) },
            { XMFLOAT3(0.2f,  0.2f, 0.f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2(1.f, 0.f) },
            { XMFLOAT3(0.2f, -0.2f, 0.f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2(1.f, 1.f) },
            { XMFLOAT3(-0.2f, -0.2f, 0.f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f), XMFLOAT2(0.f, 1.f) },
          }
        };

        D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
        vertexBufferData.pSysMem = quadVertices.data();
        vertexBufferData.SysMemPitch = 0;
        vertexBufferData.SysMemSlicePitch = 0;
        const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(VertexPositionColorTex) * quadVertices.size(), D3D11_BIND_VERTEX_BUFFER);
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBuffer(&vertexBufferDesc, &vertexBufferData, &m_vertexBuffer));

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
        const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(unsigned short) * quadIndices.size(), D3D11_BIND_INDEX_BUFFER);
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&indexBufferDesc, &indexBufferData, &m_indexBuffer));

        D3D11_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(D3D11_SAMPLER_DESC));
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
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateSamplerState(&desc, &m_quadTextureSamplerState));

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

        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBlendState(&blendDesc, &m_blendState));

        // After the assets are loaded, the quad is ready to be rendered.
        m_componentReady = true;
      });
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;

      m_textRenderer->ReleaseDeviceDependentResources();
      m_textRenderer = nullptr;
      m_distanceFieldRenderer->ReleaseDeviceDependentResources();
      m_distanceFieldRenderer = nullptr;

      m_vertexShader.Reset();
      m_inputLayout.Reset();
      m_pixelShader.Reset();
      m_geometryShader.Reset();

      m_constantBuffer.Reset();

      m_vertexBuffer.Reset();
      m_indexBuffer.Reset();

      m_quadTextureSamplerState.Reset();
    }
  }
}