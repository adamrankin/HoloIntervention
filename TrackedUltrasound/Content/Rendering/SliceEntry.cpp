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
#include "SliceEntry.h"

using namespace SimpleMath;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    SliceEntry::SliceEntry( uint32 width, uint32 height )
      : m_id( 0 )
      , m_width( width )
      , m_height( height )
      , m_imageData( nullptr )
      , m_showing( true )
      , m_currentPose( SimpleMath::Matrix::Identity )
      , m_lastPose( SimpleMath::Matrix::Identity )
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    SliceEntry::~SliceEntry()
    {
      ReleaseDeviceDependentResources();

      if ( m_imageData != nullptr )
      {
        delete m_imageData;
      }
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Update( const DX::StepTimer& timer )
    {
      const float& deltaTime = static_cast<float>( timer.GetElapsedSeconds() );

      Vector3 currentScale;
      Quaternion currentRotation;
      Vector3 currentTranslation;
      m_currentPose.Decompose( currentScale, currentRotation, currentTranslation );

      Vector3 desiredScale;
      Quaternion desiredRotation;
      Vector3 desiredTranslation;
      m_desiredPose.Decompose( desiredScale, desiredRotation, desiredTranslation );

      Vector3 smoothedScale = Vector3::Lerp( currentScale, desiredScale, deltaTime * LERP_RATE );
      Quaternion smoothedRotation = Quaternion::Slerp( currentRotation, desiredRotation, deltaTime * LERP_RATE );
      Vector3 smoothedTranslation = Vector3::Lerp( currentTranslation, desiredTranslation, deltaTime * LERP_RATE );

      m_lastPose = m_currentPose;
      m_currentPose = Matrix::CreateScale( smoothedScale ) * Matrix::CreateFromQuaternion( smoothedRotation ) * Matrix::CreateTranslation( smoothedTranslation );
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Render( uint32 indexCount )
    {
      if ( !m_showing )
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      // Apply the model constant buffer to the vertex shader.
      context->VSSetConstantBuffers(
        0,
        1,
        m_sliceConstantBuffer.GetAddressOf()
      );

      // Each vertex is one instance of the VertexPositionColor struct.
      const UINT stride = sizeof( VertexPositionTexture );
      const UINT offset = 0;
      context->IASetVertexBuffers(
        0,
        1,
        m_vertexBuffer.GetAddressOf(),
        &stride,
        &offset
      );

      context->PSSetShaderResources(
        0,
        1,
        m_shaderResourceView.GetAddressOf()
      );

      // Draw the objects.
      context->DrawIndexedInstanced( indexCount, 2, 0, 0, 0 );
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetImageData( byte* imageData )
    {
      this->m_imageData = imageData;

      // Copy data to texture subresource
      const auto context = m_deviceResources->GetD3DDeviceContext();
      context->UpdateSubresource( m_texture.Get(), 0, nullptr, imageData, 0, 0 );
    }

    //----------------------------------------------------------------------------
    byte* SliceEntry::GetImageData() const
    {
      return m_imageData;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::CreateDeviceDependentResources()
    {
      // Create the texture that will store the image data
      // TODO : support more formats?
      CD3D11_TEXTURE2D_DESC textureDesc( DXGI_FORMAT_R8_UNORM, m_width, m_height, 1, 1, D3D11_BIND_SHADER_RESOURCE );
      m_deviceResources->GetD3DDevice()->CreateTexture2D( &textureDesc, nullptr, &m_texture );

      // Create read and write views for the off-screen render target.
      m_deviceResources->GetD3DDevice()->CreateShaderResourceView( m_texture.Get(), nullptr, &m_shaderResourceView );

      const CD3D11_BUFFER_DESC constantBufferDesc( sizeof( SliceConstantBuffer ), D3D11_BIND_CONSTANT_BUFFER );
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &constantBufferDesc,
          nullptr,
          &m_sliceConstantBuffer
        )
      );

      // TODO : these vertices can be changed to transform the texture, I think, perhaps that should be in the world matrix?
      // If so, move this back to the slice renderer to save on VRAM
      static const std::array<VertexPositionTexture, 4> quadVertices =
      {
        {
          { XMFLOAT3( -0.2f,  0.2f, 0.f ), XMFLOAT2( 0.f, 0.f ) },
          { XMFLOAT3( 0.2f,  0.2f, 0.f ), XMFLOAT2( 1.f, 0.f ) },
          { XMFLOAT3( 0.2f, -0.2f, 0.f ), XMFLOAT2( 1.f, 1.f ) },
          { XMFLOAT3( -0.2f, -0.2f, 0.f ), XMFLOAT2( 0.f, 1.f ) },
        }
      };

      D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
      vertexBufferData.pSysMem = quadVertices.data();
      vertexBufferData.SysMemPitch = 0;
      vertexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC vertexBufferDesc( sizeof( VertexPositionTexture ) * quadVertices.size(), D3D11_BIND_VERTEX_BUFFER );
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &vertexBufferDesc,
          &vertexBufferData,
          &m_vertexBuffer
        )
      );
    }

    //----------------------------------------------------------------------------
    void SliceEntry::ReleaseDeviceDependentResources()
    {
      m_vertexBuffer.Reset();
      m_sliceConstantBuffer.Reset();
      m_texture.Reset();
      m_shaderResourceView.Reset();
    }
  }
}