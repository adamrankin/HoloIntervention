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

// Windows includes
#include <ppltasks.h>

using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Storage;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation )
      : m_deviceResources( deviceResources )
      , m_assetLocation( assetLocation )
    {
      // Validate asset location
      Platform::String^ mainFolderLocation = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;

      auto folderTask = Concurrency::create_task( StorageFolder::GetFolderFromPathAsync( mainFolderLocation ) );
      StorageFolder^ mainFolder = folderTask.get();

      std::string asset( m_assetLocation.begin(), m_assetLocation.end() );

      char drive[32];
      char dir[32767];
      char name[2048];
      char ext[32];
      _splitpath_s( asset.c_str(), drive, dir, name, ext );

      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    ModelEntry::~ModelEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Update( const DX::StepTimer& timer )
    {
      if ( !m_enableModel )
      {
        // No need to update, cursor is not drawn
        return;
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Render()
    {
      // Loading is asynchronous. Resources must be created before drawing can occur.
      if ( !m_loadingComplete || !m_enableModel )
      {
        return;
      }

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
    }

    //----------------------------------------------------------------------------
    void ModelEntry::CreateDeviceDependentResources()
    {
      m_states = std::make_unique<DirectX::CommonStates>( m_deviceResources->GetD3DDevice() );
      m_effectFactory = std::make_unique<DirectX::InstancedEffectFactory>( m_deviceResources->GetD3DDevice() );
      try
      {
        m_model = std::shared_ptr<DirectX::Model>( std::move( Model::CreateFromCMO( m_deviceResources->GetD3DDevice(), m_assetLocation.c_str(), *m_effectFactory ) ) );
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
    void ModelEntry::EnableModel( bool enable )
    {
      m_enableModel = enable;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::ToggleEnabled()
    {
      m_enableModel = !m_enableModel;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsModelEnabled() const
    {
      return m_enableModel;
    }

    //----------------------------------------------------------------------------
    uint32 ModelEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetId( uint32 id )
    {
      m_id = id;
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

        auto imatrices = dynamic_cast<IEffectMatrices*>( part->effect.get() );
        if ( imatrices )
        {
          // TODO : determine how to set world matrix
          imatrices->SetMatrices( SimpleMath::Matrix::Identity, SimpleMath::Matrix::Identity, SimpleMath::Matrix::Identity );
        }

        DrawMeshPart( *part );
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::DrawMeshPart( const DirectX::ModelMeshPart& part )
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

      m_deviceResources->GetD3DDeviceContext()->DrawIndexedInstanced( part.indexCount, 2, part.startIndex, part.vertexOffset, 0 );
    }
  }
}