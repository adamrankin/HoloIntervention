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
#include "ModelEntry.h"

// DirectXTK includes
#include <DirectXHelper.h>
#include <InstancedEffects.h>

// Windows includes
#include <ppltasks.h>

// std includes
#include <algorithm>

using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation )
      : m_deviceResources( deviceResources )
      , m_assetLocation( assetLocation )
      , m_worldMatrix( 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f )
    {
      // Validate asset location
      Platform::String^ mainFolderLocation = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;

      auto folderTask = Concurrency::create_task( StorageFolder::GetFolderFromPathAsync( mainFolderLocation ) ).then( [this]( StorageFolder ^ folder )
      {
        std::string asset( m_assetLocation.begin(), m_assetLocation.end() );

        char drive[32];
        char dir[32767];
        char name[2048];
        char ext[32];
        _splitpath_s( asset.c_str(), drive, dir, name, ext );

        std::string nameStr( name );
        std::string extStr( ext );
        std::string dirStr( dir );
        std::replace( dirStr.begin(), dirStr.end(), '/', '\\' );
        std::wstring wdir( dirStr.begin(), dirStr.end() );
        Concurrency::create_task( folder->GetFolderAsync( ref new Platform::String( wdir.c_str() ) ) ).then( [this, nameStr, extStr]( concurrency::task<StorageFolder^> previousTask )
        {
          StorageFolder^ folder;
          try
          {
            folder = previousTask.get();
          }
          catch ( Platform::InvalidArgumentException^ e )
          {
            return;
          }
          catch ( const std::exception& )
          {
            return;
          }
          std::string filename( nameStr );
          filename.append( extStr );
          std::wstring wFilename( filename.begin(), filename.end() );

          Concurrency::create_task( folder->GetFileAsync( ref new Platform::String( wFilename.c_str() ) ) ).then( [ this ]( StorageFile ^ file )
          {
            if ( file != nullptr )
            {
              CreateDeviceDependentResources();
            }
          } );
        } );
      } );
    }

    //----------------------------------------------------------------------------
    ModelEntry::~ModelEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Update( const DX::StepTimer& timer, const DX::ViewProjection& vp )
    {
      m_viewProjection = vp;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Render()
    {
      // Loading is asynchronous. Resources must be created before drawing can occur.
      if ( !m_loadingComplete || !m_visible )
      {
        return;
      }

      // TODO : check to see if the model is in front of the camera...

      const auto context = m_deviceResources->GetD3DDeviceContext();

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

      // Clean up after rendering
      m_deviceResources->GetD3DDeviceContext()->OMSetBlendState( nullptr, nullptr, 0xffffffff );
      m_deviceResources->GetD3DDeviceContext()->OMSetDepthStencilState( nullptr, 0 );
      m_deviceResources->GetD3DDeviceContext()->RSSetState( nullptr );
    }

    //----------------------------------------------------------------------------
    void ModelEntry::CreateDeviceDependentResources()
    {
      m_states = std::make_unique<DirectX::CommonStates>( m_deviceResources->GetD3DDevice() );
      m_effectFactory = std::make_unique<DirectX::InstancedEffectFactory>( m_deviceResources->GetD3DDevice() );
      try
      {
        m_model = std::shared_ptr<DirectX::Model>( std::move( DirectX::Model::CreateFromCMO( m_deviceResources->GetD3DDevice(), m_assetLocation.c_str(), *m_effectFactory ) ) );
      }
      catch ( const std::exception& e )
      {
        OutputDebugStringA( e.what() );
        return;
      }

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::ReleaseDeviceDependentResources()
    {
      m_loadingComplete = false;
      m_model = nullptr;
      m_effectFactory = nullptr;
      m_states = nullptr;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetVisible( bool enable )
    {
      m_visible = enable;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::ToggleVisible()
    {
      m_visible = !m_visible;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetWorld( const float4x4& world )
    {
      m_worldMatrix = world;
    }

    //----------------------------------------------------------------------------
    uint64 ModelEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetId( uint64 id )
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderGreyscale()
    {
      UpdateEffects( [ = ]( DirectX::IEffect * effect ) -> void
      {
      } );
      m_renderingState = RENDERING_GREYSCALE;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderDefault()
    {
      UpdateEffects( [ = ]( DirectX::IEffect * effect ) -> void
      {
      } );
      m_renderingState = RENDERING_DEFAULT;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::DrawMesh( const DirectX::ModelMesh& mesh, bool alpha )
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

        auto imatrices = dynamic_cast<DirectX::IStereoEffectMatrices*>( part->effect.get() );
        if ( imatrices )
        {
          DirectX::XMMATRIX view[2] = { DirectX::XMLoadFloat4x4( &m_viewProjection.view[0] ), DirectX::XMLoadFloat4x4( &m_viewProjection.view[1] ) };
          DirectX::XMMATRIX proj[2] = { DirectX::XMLoadFloat4x4( &m_viewProjection.projection[0] ), DirectX::XMLoadFloat4x4( &m_viewProjection.projection[1] ) };

          imatrices->SetMatrices( DirectX::XMLoadFloat4x4( &m_worldMatrix ), view, proj );
        }

        DrawMeshPart( *part );
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::DrawMeshPart( const DirectX::ModelMeshPart& part, std::function<void __cdecl()> setCustomState )
    {
      m_deviceResources->GetD3DDeviceContext()->IASetInputLayout( part.inputLayout.Get() );

      auto vb = part.vertexBuffer.Get();
      UINT vbStride = part.vertexStride;
      UINT vbOffset = 0;
      m_deviceResources->GetD3DDeviceContext()->IASetVertexBuffers( 0, 1, &vb, &vbStride, &vbOffset );
      m_deviceResources->GetD3DDeviceContext()->IASetIndexBuffer( part.indexBuffer.Get(), part.indexFormat, 0 );

      assert( part.effect != nullptr );
      part.effect->Apply( m_deviceResources->GetD3DDeviceContext() );

      // Hook lets the caller replace our shaders or state settings with whatever else they see fit.
      if ( setCustomState )
      {
        setCustomState();
      }

      m_deviceResources->GetD3DDeviceContext()->IASetPrimitiveTopology( part.primitiveType );

      m_deviceResources->GetD3DDeviceContext()->DrawIndexedInstanced( part.indexCount, 2, part.startIndex, part.vertexOffset, 0 );
    }

    //----------------------------------------------------------------------------
    void ModelEntry::UpdateEffects( _In_ std::function<void __cdecl( DirectX::IEffect* )> setEffect )
    {
      m_model->UpdateEffects( setEffect );
    }
  }
}