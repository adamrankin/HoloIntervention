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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "ModelEntry.h"

namespace DX
{
  class CameraResources;
  class DeviceResources;
  class StepTimer;
}

namespace DirectX
{
  class InstancedGeometricPrimitive;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    enum PrimitiveType
    {
      PrimitiveType_NONE,
      PrimitiveType_CUBE,
      PrimitiveType_BOX,
      PrimitiveType_SPHERE,
      PrimitiveType_GEOSPHERE,
      PrimitiveType_CYLINDER,
      PrimitiveType_CONE,
      PrimitiveType_TORUS,
      PrimitiveType_TETRAHEDRON,
      PrimitiveType_OCTAHEDRON,
      PrimitiveType_DODECAHEDRON,
      PrimitiveType_ICOSAHEDRON,
      PrimitiveType_TEAPOT
    };

    class ModelRenderer : public IEngineComponent
    {
      typedef std::list<std::shared_ptr<ModelEntry>> ModelList;

    public:
      ModelRenderer(const std::shared_ptr<DX::DeviceResources> deviceResources, DX::StepTimer& timer);
      ~ModelRenderer();

      // D3D resources
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update(const DX::CameraResources* cameraResources);
      void Render();

      concurrency::task<uint64> AddModelAsync(const std::wstring& assetLocation);
      concurrency::task<uint64> AddPrimitiveAsync(PrimitiveType type, Windows::Foundation::Numerics::float3 argument, size_t tessellation = 16, bool rhcoords = true, bool invertn = false);
      concurrency::task<uint64> AddPrimitiveAsync(const std::wstring& primitiveName, Windows::Foundation::Numerics::float3 argument, size_t tessellation = 16, bool rhcoords = true, bool invertn = false);
      void RemoveModel(uint64 modelId);
      std::shared_ptr<ModelEntry> GetModel(uint64 modelId) const;

    protected:
      bool FindModel(uint64 modelId, std::shared_ptr<ModelEntry>& modelEntry) const;
      static PrimitiveType StringToPrimitive(const std::wstring& primitiveName);
      std::unique_ptr<DirectX::InstancedGeometricPrimitive> CreatePrimitive(PrimitiveType type, Windows::Foundation::Numerics::float3 argument, size_t tessellation, bool rhcoords, bool invertn);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources = nullptr;
      const DX::CameraResources*                      m_cameraResources = nullptr;
      DX::StepTimer&                                  m_timer;

      // Lock protection when accessing image list
      std::mutex                                      m_modelListMutex;
      ModelList                                       m_models;
      std::mutex                                      m_idMutex;
      uint64                                          m_nextUnusedId = 1; // start at 1, 0 (INVALID_ENTRY) is considered invalid
    };
  }
}