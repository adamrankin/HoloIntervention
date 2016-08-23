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
#include "AppView.h"
#include "Common.h"
#include "GazeSystem.h"

// Rendering includes
#include "ModelRenderer.h"

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Gaze
  {
    const std::wstring GazeSystem::GAZE_CURSOR_ASSET_LOCATION = L"Assets/Models/gaze_cursor.cmo";

    //----------------------------------------------------------------------------
    GazeSystem::GazeSystem()
      : m_modelEntry( nullptr )
      , m_modelToken( 0 )
    {
      m_modelToken = HoloIntervention::instance()->GetModelRenderer().AddModel( GAZE_CURSOR_ASSET_LOCATION );
      m_modelEntry = HoloIntervention::instance()->GetModelRenderer().GetModel( m_modelToken );
	  //m_modelEntry->EnableModel(false);
    }

    //----------------------------------------------------------------------------
    GazeSystem::~GazeSystem()
    {
    }

    //----------------------------------------------------------------------------
    void GazeSystem::Update( const DX::StepTimer& timer, const float3& hitPosition, const float3& hitNormal )
    {
      m_lastHitNormal = hitNormal;
      m_lastHitPosition = hitPosition;

      // Infinite number of vectors that cross to give the normal vector, so choose arbitrary y-up vector to create a coordinate system
      float3 yUp( 0.f, 1.f, 0.f );
      float3 jVec = normalize( hitNormal );

      float3 iVec = cross( hitNormal, yUp );
      iVec = normalize( iVec );

      m_modelEntry->SetWorld( make_float4x4_world( hitPosition, jVec, iVec ) );
    }

    //----------------------------------------------------------------------------
    void GazeSystem::EnableCursor( bool enable )
    {
      m_systemEnabled = enable;
    }

    //----------------------------------------------------------------------------
    bool GazeSystem::IsCursorEnabled()
    {
      return m_systemEnabled;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& GazeSystem::GetHitPosition() const
    {
      return m_lastHitPosition;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::Numerics::float3& GazeSystem::GetHitNormal() const
    {
      return m_lastHitNormal;
    }

  }
}