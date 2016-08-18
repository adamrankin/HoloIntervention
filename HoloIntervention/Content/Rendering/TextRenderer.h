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

#pragma once

#include "DeviceResources.h"

namespace HoloIntervention
{
  namespace Rendering
  {
    class TextRenderer
    {
    public:
      TextRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources,
                    unsigned int const& textureWidth,
                    unsigned int const& textureHeight );
      ~TextRenderer();

      void RenderTextOffscreen( const std::wstring& str );

      void SetFont(const std::wstring& fontName, DWRITE_FONT_WEIGHT fontWeight, DWRITE_FONT_STYLE fontStyle, DWRITE_FONT_STRETCH fontStretch, float fontSize, const std::wstring& locale = L"");

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      ID3D11ShaderResourceView* GetTexture() const;
      ID3D11SamplerState* GetSampler() const;

    protected:
      // Cached pointer to device resources.
      const std::shared_ptr<DX::DeviceResources>          m_deviceResources;

      // Direct3D resources for rendering text to an off-screen render target.
      Microsoft::WRL::ComPtr<ID3D11Texture2D>             m_texture;
      Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>    m_shaderResourceView;
      Microsoft::WRL::ComPtr<ID3D11SamplerState>          m_pointSampler;
      Microsoft::WRL::ComPtr<ID3D11RenderTargetView>      m_renderTargetView;
      Microsoft::WRL::ComPtr<ID2D1RenderTarget>           m_d2dRenderTarget;
      Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>        m_whiteBrush;
      Microsoft::WRL::ComPtr<IDWriteTextFormat>           m_textFormat;

      // CPU-based variables for configuring the off-screen render target.
      const unsigned int                                  m_textureWidth;
      const unsigned int                                  m_textureHeight;

      // The font used to create the text
      std::wstring                                        m_font = L"Consolas";
    };
  }
}