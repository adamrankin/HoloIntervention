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
#include "DeviceResources.h"
#include "ModelEntry.h"
#include "StepTimer.h"

using namespace Windows::Foundation;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Rendering
  {
    static const uint64 INVALID_MODEL_ENTRY = 0;

    class ModelRenderer
    {
      typedef std::list<std::shared_ptr<ModelEntry>> ModelList;
    public:
      ModelRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      ~ModelRenderer();

      // D3D resources
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Update( const DX::StepTimer& timer, const DX::ViewProjection& vp );
      void Render();

      uint64 AddModel( const std::wstring& assetLocation );
      void RemoveModel( uint64 modelId );
      std::shared_ptr<ModelEntry> GetModel( uint64 modelId ) const;

    protected:
      bool FindModel( uint64 modelId, std::shared_ptr<ModelEntry>& modelEntry ) const;

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources = nullptr;

      // Lock protection when accessing image list
      std::mutex                                      m_modelListMutex;
      ModelList                                       m_models;
      uint64                                          m_nextUnusedModelId = 1; // start at 1, 0 (INVALID_MODEL_ENTRY) is considered invalid
    };
  }
}