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

// DirectXTK includes
#include <DDSTextureLoader.h>

using namespace DirectX;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    const double NotificationRenderer::MAXIMUM_REQUESTED_DURATION_SEC = 10.0;
    const double NotificationRenderer::DEFAULT_NOTIFICATION_DURATION_SEC = 3.0;
    const uint32 NotificationRenderer::BLUR_TARGET_WIDTH_PIXEL = 256;
    const uint32 NotificationRenderer::OFFSCREEN_RENDER_TARGET_WIDTH_PIXEL = 2048;
    const DirectX::XMFLOAT4 NotificationRenderer::SHOWING_ALPHA_VALUE = XMFLOAT4( 1.f, 1.f, 1.f, 1.f );
    const DirectX::XMFLOAT4 NotificationRenderer::HIDDEN_ALPHA_VALUE = XMFLOAT4( 0.f, 0.f, 0.f, 0.f );

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
    void NotificationRenderer::QueueMessage( const std::string& message, double duration )
    {
      this->QueueMessage( std::wstring( message.begin(), message.end() ), duration );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::QueueMessage( Platform::String^ message, double duration )
    {
      this->QueueMessage( std::wstring( message->Data() ), duration );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::QueueMessage( const std::wstring& message, double duration )
    {
      duration = clamp<double>( duration, MAXIMUM_REQUESTED_DURATION_SEC, 0.1 );

      std::lock_guard<std::mutex> guard( m_messageQueueMutex );
      MessageDuration mt( message, duration );
      m_messages.push_back( mt );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Update( const DX::StepTimer& timer )
    {
      if ( !m_loadingComplete )
      {
        return;
      }

      // The following code updates any relevant timers depending on state
      auto elapsedTimeSec = timer.GetElapsedSeconds();
      if ( m_animationState == SHOWING )
      {
        // Accumulate the total time shown
        m_messageTimeElapsedSec += elapsedTimeSec;
      }

      // The following code manages state transition
      if( m_animationState == HIDDEN && m_messages.size() > 0 )
      {
        m_animationState = FADING_IN;
        m_fadeTime = c_maxFadeTime;

        GrabNextMessage();
      }
      else if ( m_animationState == SHOWING && m_messageTimeElapsedSec > m_currentMessage.second )
      {
        // The time for the current message has ended

        if ( m_messages.size() > 0 )
        {
          // There is a new message to show, switch to it, do not do any fade
          // TODO : in the future, add a blink animation of some type
          GrabNextMessage();

          // Reset timer for new message
          m_messageTimeElapsedSec = 0.0;
        }
        else
        {
          m_animationState = FADING_OUT;
          m_fadeTime = c_maxFadeTime;
        }
      }
      else if ( m_animationState == FADING_IN )
      {
        if ( !IsFading() )
        {
          // animation has finished, switch to showing
          m_animationState = SHOWING;
          m_messageTimeElapsedSec = 0.f;
        }
      }
      else if ( m_animationState == FADING_OUT )
      {
        if ( m_messages.size() > 0 )
        {
          // A message has come in while we were fading out, reverse and fade back in
          GrabNextMessage();

          m_animationState = FADING_IN;
          m_fadeTime = c_maxFadeTime - m_fadeTime; // reverse the fade
        }

        if ( !IsFading() )
        {
          // animation has finished, switch to HIDDEN
          m_animationState = HIDDEN;
        }
      }

	  if (IsShowingNotification())
	  {
		  CalculateWorldMatrix();
		  CalculateAlpha(timer);
		  CalculateVelocity(1.f / static_cast<float>(timer.GetElapsedSeconds()));

		  // Update the model transform buffer for the hologram.
		  m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(
			  m_modelConstantBuffer.Get(),
			  0,
			  nullptr,
			  &m_constantBufferData,
			  0,
			  0
		  );
	  }
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::AltRTRender()
    {
      // Ensure distance field renderer has a chance to render if the text has changed
      if ( m_distanceFieldRenderer->GetRenderCount() == 0 )
      {
        m_textRenderer->RenderTextOffscreen( m_currentMessage.first );
        m_distanceFieldRenderer->RenderDistanceField( m_textRenderer->GetTexture() );
      }
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::CalculateAlpha( const DX::StepTimer& timer )
    {
      const float deltaTime = static_cast<float>( timer.GetElapsedSeconds() );

      if ( IsFading() )
      {
        // Fade the quad in, or out.
        if ( m_animationState == FADING_IN )
        {
          const float fadeLerp = 1.f - ( m_fadeTime / c_maxFadeTime );
          m_constantBufferData.hologramColorFadeMultiplier = XMFLOAT4( fadeLerp, fadeLerp, fadeLerp, 1.f );
        }
        else
        {
          const float fadeLerp = ( m_fadeTime / c_maxFadeTime );
          m_constantBufferData.hologramColorFadeMultiplier = XMFLOAT4( fadeLerp, fadeLerp, fadeLerp, 1.f );
        }
        m_fadeTime -= deltaTime;
      }
      else
      {
        m_constantBufferData.hologramColorFadeMultiplier = ( m_animationState == SHOWING ? SHOWING_ALPHA_VALUE : HIDDEN_ALPHA_VALUE );
      }
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::CalculateWorldMatrix()
    {
      XMVECTOR facingNormal = XMVector3Normalize( -XMLoadFloat3( &m_position ) );
      XMVECTOR xAxisRotation = XMVector3Normalize( XMVectorSet( XMVectorGetZ( facingNormal ), 0.f, -XMVectorGetX( facingNormal ), 0.f ) );
      XMVECTOR yAxisRotation = XMVector3Normalize( XMVector3Cross( facingNormal, xAxisRotation ) );

      // Construct the 4x4 rotation matrix.
      XMMATRIX rotationMatrix = XMMATRIX( xAxisRotation, yAxisRotation, facingNormal, XMVectorSet( 0.f, 0.f, 0.f, 1.f ) );
      const XMMATRIX modelTranslation = XMMatrixTranslationFromVector( XMLoadFloat3( &m_position ) );
      XMStoreFloat4x4( &m_constantBufferData.worldMatrix, XMMatrixTranspose( rotationMatrix * modelTranslation ) );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::GrabNextMessage()
    {
      if ( m_messages.size() == 0 )
      {
        return;
      }
      m_currentMessage = m_messages.front();
      m_messages.pop_front();
      m_distanceFieldRenderer->ResetRenderCount();
    }

    //----------------------------------------------------------------------------
    bool NotificationRenderer::IsFading() const
    {
      return m_fadeTime > 0.f;
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

      // Draw the objects.
      context->DrawIndexedInstanced( m_indexCount, 2, 0, 0, 0 );
    }

    //----------------------------------------------------------------------------
    bool NotificationRenderer::IsShowingNotification() const
    {
      return m_animationState != HIDDEN;
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::UpdateHologramPosition( SpatialPointerPose^ pointerPose, const DX::StepTimer& timer )
    {
      const float& deltaTime = static_cast<float>( timer.GetElapsedSeconds() );

      if ( pointerPose != nullptr )
      {
        // Get the gaze direction relative to the given coordinate system.
        const float3 headPosition = pointerPose->Head->Position;
        const float3 headDirection = pointerPose->Head->ForwardDirection;

        // Offset the view to centered, lower quadrant
        const float3 offset = float3( 0.f, -0.13f, 0.f );
        constexpr float offsetDistanceFromUser = 2.2f; // meters
        const float3 offsetFromGazeAtTwoMeters = headPosition + ( float3( offsetDistanceFromUser ) * ( headDirection + offset ) );

        // Use linear interpolation to smooth the position over time
        const float3 smoothedPosition = lerp( m_position, offsetFromGazeAtTwoMeters, deltaTime * c_lerpRate );

        // This will be used as the translation component of the hologram's model transform.
        m_lastPosition = m_position;
        m_position = smoothedPosition;
      }
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
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync( L"ms-appx:///NotificationUseDistanceFieldPixelShader.cso" );

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
            { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
            { XMFLOAT3( -0.2f,  0.2f, 0.f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMFLOAT2( 0.f, 0.f ) },
            { XMFLOAT3( 0.2f,  0.2f, 0.f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMFLOAT2( 1.f, 0.f ) },
            { XMFLOAT3( 0.2f, -0.2f, 0.f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMFLOAT2( 1.f, 1.f ) },
            { XMFLOAT3( -0.2f, -0.2f, 0.f ), XMFLOAT3( 1.0f, 1.0f, 1.0f ), XMFLOAT2( 0.f, 1.f ) },
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
            0,2,3,
            0,1,2,

            // +z
            2,0,3,
            1,0,2,
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

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& NotificationRenderer::GetPosition() const
    {
      return m_position;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& NotificationRenderer::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::CalculateVelocity( float oneOverDeltaTime )
    {
      const float3 deltaPosition = m_position - m_lastPosition; // meters
      m_velocity = deltaPosition * oneOverDeltaTime; // meters per second
    }
  }
}