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

// DirectXTex includes
#include <DirectXTex.h>

using namespace SimpleMath;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    const float3 SliceEntry::LOCKED_SLICE_SCREEN_OFFSET = { 0.f, 0.f, 0.f };
    const float SliceEntry::LOCKED_SLICE_DISTANCE_OFFSET = 2.0f;
    const float SliceEntry::LERP_RATE = 2.0f;

    //----------------------------------------------------------------------------
    SliceEntry::SliceEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_id( 0 )
      , m_width( 0 )
      , m_height( 0 )
      , m_imageData( nullptr )
      , m_showing( true )
      , m_headLocked( false )
      , m_currentPose( SimpleMath::Matrix::Identity )
      , m_lastPose( SimpleMath::Matrix::Identity )
      , m_desiredPose( SimpleMath::Matrix::Identity )
      , m_pixelFormat( DXGI_FORMAT_UNKNOWN )
      , m_deviceResources( deviceResources )
    {
    }

    //----------------------------------------------------------------------------
    SliceEntry::~SliceEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Update( SpatialPointerPose^ pose, const DX::StepTimer& timer )
    {
      const float& deltaTime = static_cast<float>( timer.GetElapsedSeconds() );

      Vector3 currentScale;
      Quaternion currentRotation;
      Vector3 currentTranslation;
      m_currentPose.Decompose( currentScale, currentRotation, currentTranslation );

      m_lastPose = m_currentPose;

      // Calculate new smoothed currentPose
      if ( !m_headLocked )
      {
        Vector3 desiredScale;
        Quaternion desiredRotation;
        Vector3 desiredTranslation;
        m_desiredPose.Decompose( desiredScale, desiredRotation, desiredTranslation );

        Vector3 smoothedScale = Vector3::Lerp( currentScale, desiredScale, deltaTime * LERP_RATE );
        Quaternion smoothedRotation = Quaternion::Slerp( currentRotation, desiredRotation, deltaTime * LERP_RATE );
        Vector3 smoothedTranslation = Vector3::Lerp( currentTranslation, desiredTranslation, deltaTime * LERP_RATE );

        m_currentPose = Matrix::CreateScale( smoothedScale ) * Matrix::CreateFromQuaternion( smoothedRotation ) * Matrix::CreateTranslation( smoothedTranslation );
      }
      else
      {
        const float3 offsetFromGazeAtTwoMeters = pose->Head->Position + ( float3( LOCKED_SLICE_DISTANCE_OFFSET ) * ( pose->Head->ForwardDirection + LOCKED_SLICE_SCREEN_OFFSET ) );

        // Use linear interpolation to smooth the position over time
        float3 f3_currentTranslation = { currentTranslation.x, currentTranslation.y, currentTranslation.z };
        const float3 smoothedPosition = lerp( f3_currentTranslation, offsetFromGazeAtTwoMeters, deltaTime * LERP_RATE );

        XMVECTOR facingNormal = XMVector3Normalize( -XMLoadFloat3( &smoothedPosition ) );
        XMVECTOR xAxisRotation = XMVector3Normalize( XMVectorSet( XMVectorGetZ( facingNormal ), 0.f, -XMVectorGetX( facingNormal ), 0.f ) );
        XMVECTOR yAxisRotation = XMVector3Normalize( XMVector3Cross( facingNormal, xAxisRotation ) );

        // Construct the 4x4 pose matrix.
        XMStoreFloat4x4( &m_currentPose,
                         XMMatrixTranspose(
                           XMMATRIX( xAxisRotation,
                                     yAxisRotation,
                                     facingNormal,
                                     XMVectorSet( 0.f, 0.f, 0.f, 1.f ) ) * XMMatrixTranslationFromVector( XMLoadFloat3( &smoothedPosition ) ) ) );
      }

      m_constantBuffer.worldMatrix = m_currentPose;

      // Update the model transform buffer for the hologram.
      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(
        m_sliceConstantBuffer.Get(),
        0,
        nullptr,
        &m_constantBuffer,
        0,
        0
      );
    }

    //----------------------------------------------------------------------------
    void SliceEntry::Render( uint32 indexCount )
    {
      if ( !m_showing || m_imageData == nullptr)
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
    void SliceEntry::SetImageData( std::shared_ptr<byte*> imageData, uint16 width, uint16 height, DXGI_FORMAT pixelFormat )
    {
      if ( width != m_width || height != m_height || pixelFormat != m_pixelFormat )
      {
        m_width = width;
        m_height = height;
        m_pixelFormat = pixelFormat;
        ReleaseDeviceDependentResources();
        CreateDeviceDependentResources();
      }

      m_imageData = imageData;

      m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(
        m_texture.Get(),
        0,
        nullptr,
        *m_imageData,
        m_width * DirectX::BitsPerPixel( pixelFormat ) / 8,
        m_width * m_height * DirectX::BitsPerPixel( pixelFormat ) / 8
      );
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<byte*> SliceEntry::GetImageData() const
    {
      return m_imageData;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::SetDesiredPose( const DirectX::XMFLOAT4X4& matrix )
    {
      m_desiredPose = matrix;
    }

    //----------------------------------------------------------------------------
    void SliceEntry::CreateDeviceDependentResources()
    {
      const CD3D11_BUFFER_DESC constantBufferDesc( sizeof( SliceConstantBuffer ), D3D11_BIND_CONSTANT_BUFFER );
      DX::ThrowIfFailed( m_deviceResources->GetD3DDevice()->CreateBuffer( &constantBufferDesc, nullptr, &m_sliceConstantBuffer ) );

      if ( m_pixelFormat != DXGI_FORMAT_UNKNOWN && m_width > 0 && m_height > 0 )
      {
        // Create the texture that will store the image data
        CD3D11_TEXTURE2D_DESC textureDesc( m_pixelFormat, m_width, m_height, 1, 1, D3D11_BIND_SHADER_RESOURCE );
        DX::ThrowIfFailed( m_deviceResources->GetD3DDevice()->CreateTexture2D( &textureDesc, nullptr, &m_texture ) );
        DX::ThrowIfFailed( m_deviceResources->GetD3DDevice()->CreateShaderResourceView( m_texture.Get(), nullptr, &m_shaderResourceView ) );
      }

      // Determine x and y scaling from world matrix
      Vector3 scale;
      Quaternion rotation;
      Vector3 translate;
      m_desiredPose.Decompose( scale, rotation, translate );

      // world matrix is IJK to world in mm
      // HoloLens scale is in m
      scale /= 1000;

      // Vertices should match the aspect ratio of the image size
      float bottom = -( m_height / 2 * scale.y );
      float left = -( m_width / 2 * scale.x );
      float right = m_width / 2 * scale.x;
      float top = m_height / 2 * scale.y;

      std::array<VertexPositionTexture, 4> quadVertices;
      quadVertices[0].pos = XMFLOAT3(left, top, 0.f);
      quadVertices[0].texCoord = XMFLOAT2(0.f, 0.f);
      quadVertices[1].pos = XMFLOAT3(right, top, 0.f);
      quadVertices[1].texCoord = XMFLOAT2(1.f, 0.f);
      quadVertices[2].pos = XMFLOAT3(right, bottom, 0.f);
      quadVertices[2].texCoord = XMFLOAT2(1.f, 1.f);
      quadVertices[3].pos = XMFLOAT3(left, bottom, 0.f);
      quadVertices[3].texCoord = XMFLOAT2(0.f, 1.f);

      D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
      vertexBufferData.pSysMem = quadVertices.data();
      vertexBufferData.SysMemPitch = 0;
      vertexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC vertexBufferDesc( sizeof( VertexPositionTexture ) * quadVertices.size(), D3D11_BIND_VERTEX_BUFFER );
      DX::ThrowIfFailed( m_deviceResources->GetD3DDevice()->CreateBuffer( &vertexBufferDesc, &vertexBufferData, &m_vertexBuffer ) );
    }

    //----------------------------------------------------------------------------
    void SliceEntry::ReleaseDeviceDependentResources()
    {
      m_vertexBuffer.Reset();
      m_sliceConstantBuffer.Reset();
      m_shaderResourceView.Reset();
      m_texture.Reset();
    }
  }
}