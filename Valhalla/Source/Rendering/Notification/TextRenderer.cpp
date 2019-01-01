//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Modifications by Adam Rankin, Robarts Research Institute, 2016
//
//*********************************************************

// Local includes
#include "pch.h"
#include "TextRenderer.h"

// Common includes
#include "Rendering\DeviceResources.h"
#include "Rendering\DirectXHelper.h"

// DirectX includes
#include <d2d1helper.h>
#include <dwrite.h>
#include <DirectXColors.h>

using namespace Microsoft::WRL;

namespace Valhalla
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    TextRenderer::TextRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources, uint32 const& textureWidth, uint32 const& textureHeight)
      : m_deviceResources(deviceResources)
      , m_textureWidth(textureWidth)
      , m_textureHeight(textureHeight)
    {
      CreateDeviceDependentResources();
      SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);

      SetFontLocale(L"");
    }

    //----------------------------------------------------------------------------
    TextRenderer::~TextRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void TextRenderer::RenderTextOffscreen(const std::wstring& str)
    {
      m_deviceResources->GetD3DDeviceContext()->ClearRenderTargetView(m_renderTargetView.Get(), DirectX::Colors::Transparent);
      m_d2dRenderTarget->BeginDraw();

      ComPtr<IDWriteTextLayout> textLayout;
      m_deviceResources->GetDWriteFactory()->CreateTextLayout(str.c_str(), static_cast<UINT32>(str.length()), m_textFormat.Get(), (float)m_textureWidth, (float)m_textureHeight, &textLayout);

      DWRITE_TEXT_METRICS metrics;
      DX::ThrowIfFailed(textLayout->GetMetrics(&metrics));

      D2D1::Matrix3x2F screenTranslation = D2D1::Matrix3x2F::Translation(m_textureWidth * 0.5f, m_textureHeight * 0.5f + metrics.height * 0.5f);
      m_whiteBrush->SetTransform(screenTranslation);
      m_d2dRenderTarget->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), textLayout.Get(), m_whiteBrush.Get());

      HRESULT hr = m_d2dRenderTarget->EndDraw();

      if(hr != D2DERR_RECREATE_TARGET)
      {
        DX::ThrowIfFailed(hr);
      }
    }

    //----------------------------------------------------------------------------
    void TextRenderer::SetFont(const std::wstring& fontName, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch, float fontSize, const std::wstring& locale /*= L""*/)
    {
      DX::ThrowIfFailed(m_deviceResources->GetDWriteFactory()->CreateTextFormat(fontName.c_str(), NULL, fontWeight, fontStyle, fontStretch, fontSize, locale.c_str(), m_textFormat.ReleaseAndGetAddressOf()));
    }

    //----------------------------------------------------------------------------
    ComPtr<ID3D11Texture2D> TextRenderer::GetTexture() const
    {
      return m_texture;
    }

    //----------------------------------------------------------------------------
    bool TextRenderer::SetFontName(const std::wstring& fontName)
    {
      ComPtr<IDWriteFontCollection> fontCollection;

      if(FAILED(m_deviceResources->GetDWriteFactory()->GetSystemFontCollection(fontCollection.GetAddressOf())))
      {
        return false;
      }

      uint32 index;
      BOOL exists;

      if(FAILED(fontCollection->FindFamilyName(fontName.c_str(), &index, &exists)))
      {
        return false;
      }

      if(exists)
      {
        m_fontName = fontName;
        SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);
        return true;
      }

      return false;
    }

    //----------------------------------------------------------------------------
    void TextRenderer::SetFontWeight(DWRITE_FONT_WEIGHT fontWeight)
    {
      m_fontWeight = fontWeight;
      SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);
    }

    //----------------------------------------------------------------------------
    void TextRenderer::SetFontSize(float fontSize)
    {
      m_fontSize = fontSize;
      SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);
    }

    //----------------------------------------------------------------------------
    bool TextRenderer::SetFontLocale(const std::wstring& locale)
    {
      // TODO: localization support, strings based on resource file and identifier
      return false;
    }

    //----------------------------------------------------------------------------
    void TextRenderer::SetFontStyle(DWRITE_FONT_STYLE fontStyle)
    {
      m_fontStyle = fontStyle;
      SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);
    }

    //----------------------------------------------------------------------------
    void TextRenderer::SetFontStretch(DWRITE_FONT_STRETCH fontStretch)
    {
      m_fontStretch = fontStretch;
      SetFont(m_fontName, m_fontWeight, m_fontStyle, m_fontStretch, m_fontSize, m_fontLocale);
    }

    //----------------------------------------------------------------------------
    void TextRenderer::CreateDeviceDependentResources()
    {
      CD3D11_SAMPLER_DESC desc(D3D11_DEFAULT);
      m_deviceResources->GetD3DDevice()->CreateSamplerState(&desc, &m_pointSampler);

      CD3D11_TEXTURE2D_DESC textureDesc(DXGI_FORMAT_B8G8R8A8_UNORM, m_textureWidth, m_textureHeight, 1, 1, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET);
      m_deviceResources->GetD3DDevice()->CreateTexture2D(&textureDesc, nullptr, &m_texture);

      m_deviceResources->GetD3DDevice()->CreateShaderResourceView(m_texture.Get(), nullptr, &m_shaderResourceView);
      m_deviceResources->GetD3DDevice()->CreateRenderTargetView(m_texture.Get(), nullptr, &m_renderTargetView);

      D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);

      Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
      DX::ThrowIfFailed(m_texture.As(&dxgiSurface));
      DX::ThrowIfFailed(m_deviceResources->GetD2DFactory()->CreateDxgiSurfaceRenderTarget(dxgiSurface.Get(), &props, &m_d2dRenderTarget));

      DX::ThrowIfFailed(m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Cornsilk), &m_whiteBrush));

      SetFont(m_fontName.c_str(), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 200.0f, L"");
      DX::ThrowIfFailed(m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
      DX::ThrowIfFailed(m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    }

    //----------------------------------------------------------------------------
    void TextRenderer::ReleaseDeviceDependentResources()
    {
      m_texture.Reset();
      m_shaderResourceView.Reset();
      m_pointSampler.Reset();
      m_renderTargetView.Reset();
      m_d2dRenderTarget.Reset();
      m_whiteBrush.Reset();
      m_textFormat.Reset();
    }

    //----------------------------------------------------------------------------
    ID3D11ShaderResourceView* TextRenderer::GetTextureSRV() const
    {
      return m_shaderResourceView.Get();
    }

    //----------------------------------------------------------------------------
    ID3D11SamplerState* TextRenderer::GetSampler() const
    {
      return m_pointSampler.Get();
    }
  }
}