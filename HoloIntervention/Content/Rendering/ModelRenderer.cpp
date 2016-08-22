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
#include "ModelRenderer.h"

// Windows includes
#include <comdef.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    // Loads vertex and pixel shaders from files and instantiates the cube geometry.
    ModelRenderer::ModelRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    ModelRenderer::~ModelRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Update( const DX::StepTimer& timer, const DX::ViewProjection& vp )
    {
      for ( auto model : m_models )
      {
        model->Update( timer, vp );
      }
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Render()
    {
      for ( auto model : m_models )
      {
        if ( model->IsModelEnabled() )
        {
          model->Render();
        }
      }
    }

    //----------------------------------------------------------------------------
    uint32 ModelRenderer::AddModel( const std::wstring& assetLocation )
    {
      std::shared_ptr<ModelEntry> entry = std::make_shared<ModelEntry>( m_deviceResources, assetLocation );
      entry->SetId( m_nextUnusedModelId );
      entry->EnableModel( true );

      std::lock_guard<std::mutex> guard( m_modelListMutex );
      m_models.push_back( entry );

      m_nextUnusedModelId++;
      return m_nextUnusedModelId - 1;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::RemoveModel( uint32 modelId )
    {
      std::lock_guard<std::mutex> guard( m_modelListMutex );
      std::shared_ptr<ModelEntry> model;

      for ( auto modelIter = m_models.begin(); modelIter != m_models.end(); ++modelIter )
      {
        if ( ( *modelIter )->GetId() == modelId )
        {
          m_models.erase( modelIter );
          return;
        }
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<ModelEntry> ModelRenderer::GetModel( uint32 modelId ) const
    {
      std::shared_ptr<ModelEntry> entry;
      if ( this->FindModel( modelId, entry ) )
      {
        return entry;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    bool ModelRenderer::FindModel( uint32 modelId, std::shared_ptr<ModelEntry>& modelEntry ) const
    {
      for ( auto model : m_models )
      {
        if ( model->GetId() == modelId )
        {
          modelEntry = model;
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::CreateDeviceDependentResources()
    {
      for ( auto model : m_models )
      {
        model->CreateDeviceDependentResources();
      }
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::ReleaseDeviceDependentResources()
    {
      for ( auto model : m_models )
      {
        model->ReleaseDeviceDependentResources();
      }
    }
  }
}