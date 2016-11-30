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

#pragma once

// Local includes
#include "IEngineComponent.h"
#include "ModelEntry.h"
#include "PrimitiveEntry.h"

namespace DX
{
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
    class ModelRenderer : public IEngineComponent
    {
      typedef std::list<std::shared_ptr<ModelEntry>> ModelList;
      typedef std::list<std::shared_ptr<PrimitiveEntry>> PrimitiveList;

    public:
      ModelRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~ModelRenderer();

      // D3D resources
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update(const DX::StepTimer& timer, const DX::ViewProjection& vp);
      void Render();

      uint64 AddModel(const std::wstring& assetLocation);
      void RemoveModel(uint64 modelId);
      std::shared_ptr<ModelEntry> GetModel(uint64 modelId) const;

      uint64 AddGeometricPrimitive(std::unique_ptr<DirectX::InstancedGeometricPrimitive> primitive);
      void RemovePrimitive(uint64 primitiveId);
      std::shared_ptr<PrimitiveEntry> GetPrimitive(uint64 primitiveId) const;

    protected:
      bool FindModel(uint64 modelId, std::shared_ptr<ModelEntry>& modelEntry) const;
      bool FindPrimitive(uint64 entryId, std::shared_ptr<PrimitiveEntry>& entry) const;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources = nullptr;

      // Lock protection when accessing image list
      std::mutex                                      m_modelListMutex;
      ModelList                                       m_models;
      std::mutex                                      m_primitiveListMutex;
      PrimitiveList                                   m_primitives;
      uint64                                          m_nextUnusedId = 1; // start at 1, 0 (INVALID_ENTRY) is considered invalid
    };
  }
}