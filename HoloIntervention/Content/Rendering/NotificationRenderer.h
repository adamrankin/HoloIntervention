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

#pragma once

// Local includes
#include "StepTimer.h"
#include "DeviceResources.h"

// Rendering includes
#include "DistanceFieldRenderer.h"
#include "TextRenderer.h"

// Windows includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::Holographic;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    struct NotificationConstantBuffer
    {
      XMFLOAT4X4 worldMatrix;
      XMFLOAT4   hologramColorFadeMultiplier;
    };
    static_assert(sizeof(NotificationConstantBuffer) % 16 == 0, "Constant buffer must be a size of multiple 16.");

    class NotificationRenderer
    {
      struct VertexPositionColorTex
      {
        XMFLOAT3 pos;
        XMFLOAT4 color;
        XMFLOAT2 texCoord;
      };

    public:
      NotificationRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~NotificationRenderer();

      void Update(NotificationConstantBuffer& buffer);

      void Render();
      void RenderText(const std::wstring& message);

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources;

      // Direct3D resources for quad geometry.
      ComPtr<ID3D11InputLayout>                           m_inputLayout;
      ComPtr<ID3D11Buffer>                                m_vertexBuffer;
      ComPtr<ID3D11Buffer>                                m_indexBuffer;
      ComPtr<ID3D11VertexShader>                          m_vertexShader;
      ComPtr<ID3D11GeometryShader>                        m_geometryShader;
      ComPtr<ID3D11PixelShader>                           m_pixelShader;
      ComPtr<ID3D11Buffer>                                m_modelConstantBuffer;
      ComPtr<ID3D11BlendState>                            m_blendState;

      // Direct3D resources for the texture.
      ComPtr<ID3D11SamplerState>                          m_quadTextureSamplerState;

      // System resources for quad geometry.
      NotificationConstantBuffer                          m_constantBufferData;
      uint32                                              m_indexCount = 0;

      // Variables used with the rendering loop.
      bool                                                m_loadingComplete = false;

      // If the current D3D Device supports VPRT, we can avoid using a geometry
      // shader just to set the render target array index.
      bool                                                m_usingVprtShaders = false;

      // Text renderer
      std::unique_ptr<TextRenderer>                       m_textRenderer = nullptr;
      std::unique_ptr<DistanceFieldRenderer>              m_distanceFieldRenderer = nullptr;

      // Rendering related constants
      static const uint32 BLUR_TARGET_WIDTH_PIXEL;
      static const uint32 OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL;
    };
  }
}
