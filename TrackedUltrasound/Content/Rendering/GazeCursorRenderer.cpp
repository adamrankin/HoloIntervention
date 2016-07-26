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
#include "DirectXHelper.h"
#include "GazeCursorRenderer.h"
#include "RenderShaderStructures.h"

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    // Loads vertex and pixel shaders from files and instantiates the cube geometry.
    GazeCursorRenderer::GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
      , m_effectFactory( std::make_unique<InstancedEffectFactory>( deviceResources->GetD3DDevice() ) )
    {
      CreateDeviceDependentResourcesAsync();
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::Update( float3 gazeTargetPosition, float3 gazeTargetNormal )
    {
      if ( !m_enableCursor )
      {
        // No need to update, cursor is not drawn
        return;
      }

      // Get the gaze direction relative to the given coordinate system.
      m_gazeTargetPosition = gazeTargetPosition;
      m_gazeTargetNormal = gazeTargetNormal;

      BasicLightingConstants lightingConstants;

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource( m_constantBuffer, 0, nullptr, &lightingConstants, 0, 0 );
      m_deviceResources->GetD3DDeviceContext()->CSSetConstantBuffers( 0, 1, &m_constantBuffer );
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

      /*
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
      */
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
      return m_gazeTargetPosition;
    }

    //----------------------------------------------------------------------------
    Numerics::float3 GazeCursorRenderer::GetNormal() const
    {
      return m_gazeTargetNormal;
    }

    //----------------------------------------------------------------------------
    concurrency::task<void> GazeCursorRenderer::CreateDeviceDependentResourcesAsync()
    {
      return concurrency::create_task( [&]()
      {
        m_model = Model::CreateFromCMO( m_deviceResources->GetD3DDevice(), L"Assets/Models/gaze_cursor.cmo", *m_effectFactory );
      } );
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::ReleaseDeviceDependentResources()
    {
      m_model = nullptr;
      SAFE_RELEASE( m_constantBuffer );
    }
  }
}