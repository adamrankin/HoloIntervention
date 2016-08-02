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

// Windows includes
#include <comdef.h>

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
    {
      CreateDeviceDependentResources();
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
      XMFLOAT3 pos( gazeTargetPosition.x, gazeTargetPosition.y, gazeTargetPosition.z );
      XMFLOAT3 dir( gazeTargetNormal.x, gazeTargetNormal.y, gazeTargetNormal.z );
      XMFLOAT3 up( 0, 1, 0 );
      m_world = XMMatrixLookToLH( XMLoadFloat3( &pos ), XMLoadFloat3( &dir ), XMLoadFloat3( &up ) );

      // These are stored to be available for focus point querying
      m_gazeTargetPosition = gazeTargetPosition;
      m_gazeTargetNormal = gazeTargetNormal;
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

      if ( !m_deviceResources->GetDeviceSupportsVprt() )
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

      // Draw opaque parts
      for ( auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it )
      {
        auto mesh = it->get();
        assert( mesh != 0 );

        mesh->PrepareForRendering( context, *m_states, false, false );

        DrawMesh( *mesh, false );
      }

      // Draw alpha parts
      for ( auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it )
      {
        auto mesh = it->get();
        assert( mesh != 0 );

        mesh->PrepareForRendering( context, *m_states, true, false );

        DrawMesh( *mesh, true );
      }
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
    void GazeCursorRenderer::DrawMesh( const DirectX::ModelMesh& mesh, bool alpha )
    {
      assert( m_deviceResources->GetD3DDeviceContext() != 0 );

      for ( auto it = mesh.meshParts.cbegin(); it != mesh.meshParts.cend(); ++it )
      {
        auto part = ( *it ).get();
        assert( part != 0 );

        if ( part->isAlpha != alpha )
        {
          // Skip alpha parts when drawing opaque or skip opaque parts if drawing alpha
          continue;
        }

        auto imatrices = dynamic_cast<IEffectMatrices*>( part->effect.get() );
        if ( imatrices )
        {
          imatrices->SetMatrices( m_world, SimpleMath::Matrix::Identity, SimpleMath::Matrix::Identity );
        }

        DrawMeshPart( *part );
      }
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::DrawMeshPart( const DirectX::ModelMeshPart& part )
    {
      m_deviceResources->GetD3DDeviceContext()->IASetInputLayout( part.inputLayout.Get() );

      auto vb = part.vertexBuffer.Get();
      UINT vbStride = part.vertexStride;
      UINT vbOffset = 0;
      m_deviceResources->GetD3DDeviceContext()->IASetVertexBuffers( 0, 1, &vb, &vbStride, &vbOffset );
      m_deviceResources->GetD3DDeviceContext()->IASetIndexBuffer( part.indexBuffer.Get(), part.indexFormat, 0 );

      assert( part.effect != nullptr );
      part.effect->Apply( m_deviceResources->GetD3DDeviceContext() );

      // TODO : set any state before doing any rendering

      m_deviceResources->GetD3DDeviceContext()->IASetPrimitiveTopology( part.primitiveType );

      //m_deviceResources->GetD3DDeviceContext()->DrawIndexedInstanced( part.indexCount, 2, part.startIndex, part.vertexOffset, 0 );
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::CreateDeviceDependentResources()
    {
      m_states = std::make_unique<CommonStates>( m_deviceResources->GetD3DDevice() );
      m_effectFactory = std::make_unique<InstancedEffectFactory>( m_deviceResources->GetD3DDevice() );
      try
      {
        m_model = Model::CreateFromCMO( m_deviceResources->GetD3DDevice(), L"Assets/Models/gaze_cursor.cmo", *m_effectFactory );
      }
      catch ( const std::exception& e )
      {
        OutputDebugStringA( e.what() );
      }

      if ( !m_deviceResources->GetDeviceSupportsVprt() )
      {
        // Load a geometry shader that can pass through the render target index
        // PCCI = Position, color, color, instanceId
        auto loadGSTask = DX::ReadDataAsync( L"ms-appx:///PCCIGeometryShader.cso" );
        auto createGSTask = loadGSTask.then( [this]( const std::vector<byte>& fileData )
        {
          DX::ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateGeometryShader(
              fileData.data(),
              fileData.size(),
              nullptr,
              &m_geometryShader
            )
          );
        } ).then( [this]( Concurrency::task<void> previousTask )
        {
          try
          {
            previousTask.wait();
          }
          catch ( const std::exception& e )
          {
            OutputDebugStringA( e.what() );
          }

          m_loadingComplete = true;
        } );

      }
      else
      {
        m_loadingComplete = true;
      }
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::ReleaseDeviceDependentResources()
    {
      m_loadingComplete = false;
      m_model = nullptr;
      m_effectFactory = nullptr;
      m_states = nullptr;
    }
  }
}