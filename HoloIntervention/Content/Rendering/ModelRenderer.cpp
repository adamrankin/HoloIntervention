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
#include "DeviceResources.h"

// Windows includes
#include <comdef.h>

// DirectXTK includes
#include "InstancedGeometricPrimitive.h"

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
    ModelRenderer::ModelRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    ModelRenderer::~ModelRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Update(const DX::StepTimer& timer, const DX::ViewProjection& vp)
    {
      for (auto& model : m_models)
      {
        model->Update(timer, vp);
      }
      for (auto& primitive : m_primitives)
      {
        primitive->Update(timer, vp);
      }
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Render()
    {
      for (auto& model : m_models)
      {
        if (model->IsVisible())
        {
          model->Render();
        }
      }
      for (auto& primitive : m_primitives)
      {
        if (primitive->IsVisible())
        {
          primitive->Render();
        }
      }
    }

    //----------------------------------------------------------------------------
    uint64 ModelRenderer::AddModel(const std::wstring& assetLocation)
    {
      std::shared_ptr<ModelEntry> entry = std::make_shared<ModelEntry>(m_deviceResources, assetLocation);
      entry->SetId(m_nextUnusedId);
      entry->SetVisible(true);

      std::lock_guard<std::mutex> guard(m_modelListMutex);
      m_models.push_back(entry);

      m_nextUnusedId++;
      return m_nextUnusedId - 1;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::RemoveModel(uint64 modelId)
    {
      std::lock_guard<std::mutex> guard(m_modelListMutex);
      std::shared_ptr<ModelEntry> model;

      for (auto modelIter = m_models.begin(); modelIter != m_models.end(); ++modelIter)
      {
        if ((*modelIter)->GetId() == modelId)
        {
          m_models.erase(modelIter);
          return;
        }
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<ModelEntry> ModelRenderer::GetModel(uint64 modelId) const
    {
      std::shared_ptr<ModelEntry> entry;
      if (FindModel(modelId, entry))
      {
        return entry;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    uint64 ModelRenderer::AddPrimitive(PrimitiveType type, float diameter, size_t tessellation, bool rhcoords, bool invertn)
    {
      std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive(nullptr);
      switch (type)
      {
      case PrimitiveType_SPHERE:
        primitive = std::move(DirectX::InstancedGeometricPrimitive::CreateSphere(m_deviceResources->GetD3DDeviceContext(), diameter, tessellation, rhcoords, invertn));
        break;
      }

      std::shared_ptr<PrimitiveEntry> entry = std::make_shared<PrimitiveEntry>(m_deviceResources, std::move(primitive));
      entry->SetId(m_nextUnusedId);
      entry->SetVisible(true);

      std::lock_guard<std::mutex> guard(m_primitiveListMutex);
      m_primitives.push_back(entry);

      m_nextUnusedId++;
      return m_nextUnusedId - 1;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::RemovePrimitive(uint64 primitiveId)
    {
      std::lock_guard<std::mutex> guard(m_primitiveListMutex);
      std::shared_ptr<PrimitiveEntry> primitive;

      for (auto primIter = m_primitives.begin(); primIter != m_primitives.end(); ++primIter)
      {
        if ((*primIter)->GetId() == primitiveId)
        {
          m_primitives.erase(primIter);
          return;
        }
      }
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<PrimitiveEntry> ModelRenderer::GetPrimitive(uint64 primitiveId) const
    {
      std::shared_ptr<PrimitiveEntry> entry;
      if (FindPrimitive(primitiveId, entry))
      {
        return entry;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    bool ModelRenderer::FindModel(uint64 modelId, std::shared_ptr<ModelEntry>& modelEntry) const
    {
      for (auto model : m_models)
      {
        if (model->GetId() == modelId)
        {
          modelEntry = model;
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    bool ModelRenderer::FindPrimitive(uint64 primitiveId, std::shared_ptr<PrimitiveEntry>& entry) const
    {
      for (auto primitive : m_primitives)
      {
        if (primitive->GetId() == primitiveId)
        {
          entry = primitive;
          return true;
        }
      }

      return false;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::CreateDeviceDependentResources()
    {
      for (auto model : m_models)
      {
        model->CreateDeviceDependentResources();
      }
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;
      for (auto model : m_models)
      {
        model->ReleaseDeviceDependentResources();
      }
    }
  }
}