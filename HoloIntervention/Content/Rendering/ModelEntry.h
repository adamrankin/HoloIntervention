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
#include "StepTimer.h"
#include "DeviceResources.h"
#include "InstancedBasicEffect.h"
#include "InstancedEffectFactory.h"

// DirectXTK includes
#include <CommonStates.h>
#include <Effects.h>
#include <Model.h>
#include <SimpleMath.h>

using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelEntry
    {
    public:
      ModelEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation );
      ~ModelEntry();

      void Update( const DX::StepTimer& timer );
      void Render();

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Model enable control
      void EnableModel( bool enable );
      void ToggleEnabled();
      bool IsModelEnabled() const;

      // Accessors
      uint32 GetId() const;
      void SetId( uint32 id );

    protected:
      void DrawMesh( const DirectX::ModelMesh& mesh, bool alpha );
      void DrawMeshPart( const DirectX::ModelMeshPart& part );

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources = nullptr;

      // DirectXTK resources for the cursor model
      std::unique_ptr<DirectX::CommonStates>              m_states;
      std::unique_ptr<DirectX::InstancedEffectFactory>    m_effectFactory = nullptr;
      std::shared_ptr<DirectX::Model>                     m_model = nullptr;
      std::wstring                                        m_assetLocation;

      // TODO : add geometry shader support

      // Model related behavior
      bool                                                m_enableModel = false;
      uint32                                              m_id;

      // Variables used with the rendering loop.
      bool                                                m_loadingComplete = false;
    };
  }
}