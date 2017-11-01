/*====================================================================
Copyright(c) 2017 Adam Rankin


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
#include "CameraResources.h"
#include "Common.h"
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "ModelRenderer.h"

// Windows includes
#include <comdef.h>

// DirectXTK includes
#include "InstancedGeometricPrimitive.h"

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    // Loads vertex and pixel shaders from files and instantiates the cube geometry.
    ModelRenderer::ModelRenderer(const std::shared_ptr<DX::DeviceResources> deviceResources, DX::StepTimer& timer)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    ModelRenderer::~ModelRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Update(const DX::CameraResources* cameraResources)
    {
#if defined(_DEBUG)
      if (cameraResources == nullptr)
      {
        // When coming back from a breakpoint, this is sometimes null
        return;
      }
#endif

      m_cameraResources = cameraResources;

      for (auto& model : m_models)
      {
        model->Update(cameraResources);
      }
      for (auto& primitive : m_primitives)
      {
        primitive->Update(cameraResources);
      }
    }

    //----------------------------------------------------------------------------
    void ModelRenderer::Render()
    {
#if defined(_DEBUG)
      if (m_cameraResources == nullptr)
      {
        return;
      }
#endif

      SpatialBoundingFrustum frustum;
      m_cameraResources->GetLatestSpatialBoundingFrustum(frustum);

      for (auto& model : m_models)
      {
        if (model->IsVisible() && model->IsInFrustum(frustum))
        {
          model->Render();
        }
      }
      for (auto& primitive : m_primitives)
      {
        if (primitive->IsVisible() && primitive->IsInFrustum(frustum))
        {
          primitive->Render();
        }
      }
    }

    //----------------------------------------------------------------------------
    task<uint64> ModelRenderer::AddModelAsync(const std::wstring& assetLocation)
    {
      return create_task([this, assetLocation]()
      {
        uint64 myId(0);
        std::shared_ptr<ModelEntry> entry = std::make_shared<ModelEntry>(m_deviceResources, assetLocation, m_timer);
        {
          std::lock_guard<std::mutex> guard(m_idMutex);
          entry->SetId(m_nextUnusedId++);
          myId = m_nextUnusedId - 1;
        }
        entry->SetVisible(true);

        {
          std::lock_guard<std::mutex> guard(m_modelListMutex);
          m_models.push_back(entry);
        }

        while (!entry->IsLoaded() && !entry->FailedLoad())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return myId;
      });
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
    task<uint64> ModelRenderer::AddPrimitiveAsync(PrimitiveType type, float3 argument, size_t tessellation, bool rhcoords, bool invertn)
    {
      return create_task([this, type, argument, tessellation, rhcoords, invertn]()
      {
        std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive = CreatePrimitive(type, argument, tessellation, rhcoords, invertn);

        if (primitive == nullptr)
        {
          LOG_ERROR(L"Unable to create primitive, unknown type.");
          return INVALID_TOKEN;
        }

        std::shared_ptr<PrimitiveEntry> entry = std::make_shared<PrimitiveEntry>(m_deviceResources, std::move(primitive), m_timer);
        entry->SetId(m_nextUnusedId);
        entry->SetVisible(true);

        std::lock_guard<std::mutex> guard(m_primitiveListMutex);
        m_primitives.push_back(entry);

        m_nextUnusedId++;
        return m_nextUnusedId - 1;
      });
    }

    //----------------------------------------------------------------------------
    task<uint64> ModelRenderer::AddPrimitiveAsync(const std::wstring& primitiveName, float3 argument, size_t tessellation, bool rhcoords, bool invertn)
    {
      return create_task([this, primitiveName, argument, tessellation, rhcoords, invertn]()
      {
        PrimitiveType type = ModelRenderer::StringToPrimitive(primitiveName);
        std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive = CreatePrimitive(type, argument, tessellation, rhcoords, invertn);

        if (primitive == nullptr)
        {
          LOG_ERROR(L"Unable to create primitive, unknown type.");
          return INVALID_TOKEN;
        }

        std::shared_ptr<PrimitiveEntry> entry = std::make_shared<PrimitiveEntry>(m_deviceResources, std::move(primitive), m_timer);
        entry->SetId(m_nextUnusedId);
        entry->SetVisible(true);

        std::lock_guard<std::mutex> guard(m_primitiveListMutex);
        m_primitives.push_back(entry);

        m_nextUnusedId++;
        return m_nextUnusedId - 1;
      });
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
    Rendering::PrimitiveType ModelRenderer::StringToPrimitive(const std::wstring& primitiveName)
    {
      if (IsEqualInsensitive(primitiveName, L"CUBE"))
      {
        return PrimitiveType_CUBE;
      }
      else if (IsEqualInsensitive(primitiveName, L"BOX"))
      {
        return PrimitiveType_BOX;
      }
      else if (IsEqualInsensitive(primitiveName, L"SPHERE"))
      {
        return PrimitiveType_SPHERE;
      }
      else if (IsEqualInsensitive(primitiveName, L"GEOSPHERE"))
      {
        return PrimitiveType_GEOSPHERE;
      }
      else if (IsEqualInsensitive(primitiveName, L"CYLINDER"))
      {
        return PrimitiveType_CYLINDER;
      }
      else if (IsEqualInsensitive(primitiveName, L"CONE"))
      {
        return PrimitiveType_CONE;
      }
      else if (IsEqualInsensitive(primitiveName, L"TORUS"))
      {
        return PrimitiveType_TORUS;
      }
      else if (IsEqualInsensitive(primitiveName, L"TETRAHEDRON"))
      {
        return PrimitiveType_TETRAHEDRON;
      }
      else if (IsEqualInsensitive(primitiveName, L"OCTAHEDRON"))
      {
        return PrimitiveType_OCTAHEDRON;
      }
      else if (IsEqualInsensitive(primitiveName, L"DODECAHEDRON"))
      {
        return PrimitiveType_DODECAHEDRON;
      }
      else if (IsEqualInsensitive(primitiveName, L"ICOSAHEDRON"))
      {
        return PrimitiveType_ICOSAHEDRON;
      }
      else if (IsEqualInsensitive(primitiveName, L"TEAPOT"))
      {
        return PrimitiveType_TEAPOT;
      }
      else
      {
        return PrimitiveType_NONE;
      }
    }

    //----------------------------------------------------------------------------
    std::unique_ptr<DirectX::InstancedGeometricPrimitive> ModelRenderer::CreatePrimitive(PrimitiveType type, float3 argument, size_t tessellation, bool rhcoords, bool invertn)
    {
      XMFLOAT3 vec;
      vec.x = argument.x;
      vec.y = argument.y;
      vec.z = argument.z;

      switch (type)
      {
      case PrimitiveType_CUBE:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateCube(m_deviceResources->GetD3DDeviceContext(), argument.x, rhcoords));
      case PrimitiveType_BOX:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateBox(m_deviceResources->GetD3DDeviceContext(), vec, rhcoords, invertn));
      case PrimitiveType_SPHERE:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateSphere(m_deviceResources->GetD3DDeviceContext(), argument.x, tessellation, rhcoords, invertn));
      case PrimitiveType_GEOSPHERE:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateGeoSphere(m_deviceResources->GetD3DDeviceContext(), argument.x, tessellation, rhcoords));
      case PrimitiveType_CYLINDER:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateCylinder(m_deviceResources->GetD3DDeviceContext(), argument.x, argument.y, tessellation, rhcoords));
      case PrimitiveType_CONE:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateCone(m_deviceResources->GetD3DDeviceContext(), argument.x, argument.y, tessellation, rhcoords));
      case PrimitiveType_TORUS:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateTorus(m_deviceResources->GetD3DDeviceContext(), argument.x, argument.y, tessellation, rhcoords));
      case PrimitiveType_TETRAHEDRON:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateTetrahedron(m_deviceResources->GetD3DDeviceContext(), argument.x, rhcoords));
      case PrimitiveType_OCTAHEDRON:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateOctahedron(m_deviceResources->GetD3DDeviceContext(), argument.x, rhcoords));
      case PrimitiveType_DODECAHEDRON:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateDodecahedron(m_deviceResources->GetD3DDeviceContext(), argument.x, rhcoords));
      case PrimitiveType_ICOSAHEDRON:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateIcosahedron(m_deviceResources->GetD3DDeviceContext(), argument.x, rhcoords));
      case PrimitiveType_TEAPOT:
        return std::move(DirectX::InstancedGeometricPrimitive::CreateTeapot(m_deviceResources->GetD3DDeviceContext(), argument.x, tessellation, rhcoords));
      }

      return nullptr;
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