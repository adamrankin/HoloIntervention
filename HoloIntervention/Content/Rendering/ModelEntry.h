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
#include "InstancedEffectFactory.h"
#include "InstancedEffects.h"
#include "StepTimer.h"

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
    static const uint64 INVALID_MODEL_ENTRY = 0;

    enum ModelRenderingState
    {
      RENDERING_DEFAULT,
      RENDERING_GREYSCALE,
    };
    class ModelEntry
    {
    public:
      ModelEntry( const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation );
      ~ModelEntry();

      void Update( const DX::StepTimer& timer, const DX::ViewProjection& vp );
      void Render();

      // D3D device related controls
      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      // Model enable control
      void SetVisible( bool enable );
      void ToggleVisible();
      bool IsVisible() const;

      // Model pose control
      void SetWorld( const float4x4& world );

      // Accessors
      uint64 GetId() const;
      void SetId( uint64 id );

      // Alternate rendering options
      void RenderGreyscale();
      void RenderDefault();

    protected:
      void DrawMesh( _In_ const DirectX::ModelMesh& mesh, _In_ bool alpha );
      void DrawMeshPart( _In_ const DirectX::ModelMeshPart& part, _In_opt_ std::function<void __cdecl()> setCustomState = nullptr );

      // Update all effects used by the model
      void __cdecl UpdateEffects(_In_ std::function<void __cdecl(DirectX::IEffect*)> setEffect);

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                m_deviceResources = nullptr;

      // DirectXTK resources for the cursor model
      std::unique_ptr<DirectX::CommonStates>              m_states = nullptr;
      std::unique_ptr<DirectX::InstancedEffectFactory>    m_effectFactory = nullptr;
      std::shared_ptr<DirectX::Model>                     m_model = nullptr;
      std::wstring                                        m_assetLocation;

      // Cached eye view projection to pass to IEffect system
      DX::ViewProjection                                  m_viewProjection;
      float4x4                                            m_worldMatrix = float4x4::identity();

      // Model related behavior
      bool                                                m_visible = false;
      uint64                                              m_id = INVALID_MODEL_ENTRY;
      ModelRenderingState                                 m_renderingState = RENDERING_DEFAULT;

      // Variables used with the rendering loop.
      bool                                                m_loadingComplete = false;
    };
  }
}