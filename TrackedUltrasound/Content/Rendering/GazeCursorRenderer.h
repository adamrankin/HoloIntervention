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
#include "CameraResources.h"
#include "DeviceResources.h"
#include "InstancedBasicEffect.h"
#include "InstancedEffectFactory.h"
#include "StepTimer.h"

//  WinRT includes
#include <ppltasks.h>

// DirectXTK includes
#include <Effects.h>
#include <Model.h>

using namespace Windows::Foundation;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Foundation::Numerics;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    class GazeCursorRenderer
    {
    public:
      GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources );
      concurrency::task<void> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();
      void Update( float3 gazeTargetPosition, float3 gazeTargetNormal );
      void Render();

      void EnableCursor( bool enable );
      void ToggleCursor();
      bool IsCursorEnabled() const;

      Numerics::float3 GetPosition() const;
      Numerics::float3 GetNormal() const;

    private:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>            m_deviceResources = nullptr;

      // Gaze origin and direction
      float3                                          m_gazeTargetPosition;
      float3                                          m_gazeTargetNormal;

      // DirectXTK resources for the cursor model
      std::unique_ptr<InstancedEffectFactory>         m_effectFactory = nullptr;
      std::unique_ptr<DirectX::Model>                 m_model = nullptr;

      // Variables used with the rendering loop.
      bool                                            m_loadingComplete = false;
      bool                                            m_enableCursor = false;
    };
  }
}