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
#include "GazeCursorRenderer.h"

// Windows includes
#include <comdef.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    // Loads vertex and pixel shaders from files and instantiates the cube geometry.
    GazeCursorRenderer::GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      CreateDeviceDependentResourcesAsync();
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::Update( float3 gazeTargetPosition, float3 gazeTargetNormal )
    {
      if ( !m_enableCursor )
      {
        // No need to update, cursor is not drawn
        return;
      }

      // Get the gaze direction relative to the given coordinate system.
      m_gazeTargetPosition = gazeTargetPosition;
      m_gazeTargetNormal = gazeTargetNormal;
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::Render()
    {
      // Loading is asynchronous. Resources must be created before drawing can occur.
      if ( !m_loadingComplete || !m_enableCursor )
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::EnableCursor( bool enable )
    {
      m_enableCursor = enable;
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::ToggleCursor()
    {
      m_enableCursor = !m_enableCursor;
    }

    //----------------------------------------------------------------------------
    bool GazeCursorRenderer::IsCursorEnabled() const
    {
      return m_enableCursor;
    }

    //----------------------------------------------------------------------------
    Numerics::float3 GazeCursorRenderer::GetPosition() const
    {
      return m_gazeTargetPosition;
    }

    //----------------------------------------------------------------------------
    Numerics::float3 GazeCursorRenderer::GetNormal() const
    {
      return m_gazeTargetNormal;
    }

    //----------------------------------------------------------------------------
    concurrency::task<void> GazeCursorRenderer::CreateDeviceDependentResourcesAsync()
    {
      return concurrency::create_task( [&]()
      {
        m_effectFactory = std::make_unique<InstancedEffectFactory>(m_deviceResources->GetD3DDevice());
        try
        {
          m_model = Model::CreateFromCMO(m_deviceResources->GetD3DDevice(), L"Assets/Models/gaze_cursor.cmo", *m_effectFactory);
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }
      } );
    }

    //----------------------------------------------------------------------------
    void GazeCursorRenderer::ReleaseDeviceDependentResources()
    {
      m_model = nullptr;
      m_effectFactory = nullptr;
    }
  }
}