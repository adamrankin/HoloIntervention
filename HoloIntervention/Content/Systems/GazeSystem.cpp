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

// Common includes
#include "StepTimer.h"

// Rendering includes
#include "ModelRenderer.h"

// System includes
#include "SpatialSystem.h"
#include "NotificationSystem.h"

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace System
  {
    const std::wstring GazeSystem::GAZE_CURSOR_ASSET_LOCATION = L"Assets/Models/gaze_cursor.cmo";

    //----------------------------------------------------------------------------
    GazeSystem::GazeSystem()
      : m_modelEntry( nullptr )
      , m_modelToken( 0 )
    {
      m_modelToken = HoloIntervention::instance()->GetModelRenderer().AddModel( GAZE_CURSOR_ASSET_LOCATION );
      m_modelEntry = HoloIntervention::instance()->GetModelRenderer().GetModel( m_modelToken );
      m_modelEntry->SetVisible( false );
    }

    //----------------------------------------------------------------------------
    GazeSystem::~GazeSystem()
    {
    }

    //----------------------------------------------------------------------------
    void GazeSystem::Update( const DX::StepTimer& timer, SpatialCoordinateSystem^ currentCoordinateSystem, SpatialPointerPose^ headPose )
    {
      if ( IsCursorEnabled() )
      {
        float3 outHitPosition;
        float3 outHitNormal;
        float3 outHitEdge;
        bool hit = HoloIntervention::instance()->GetSpatialSystem().TestRayIntersection( currentCoordinateSystem,
                   headPose->Head->Position,
                   headPose->Head->ForwardDirection,
                   outHitPosition,
                   outHitNormal,
                   outHitEdge );

        if ( hit )
        {
          // Update the gaze system with the pose to render
          m_lastHitNormal = outHitNormal;
          m_lastHitPosition = outHitPosition;
          m_lastHitEdge = outHitEdge;

          float3 iVec( outHitEdge );
          float3 kVec( outHitNormal );

          float3 jVec = -cross( iVec, kVec );
          iVec = normalize( iVec );
          // TODO : what scale is right? where is the upscale coming from? is the model in millimeters even though I specified meters?
          float4x4 matrix = make_float4x4_scale( 0.001f ) * make_float4x4_world( outHitPosition, kVec, jVec );
          m_modelEntry->SetWorld( matrix );
        }
      }
    }

    //----------------------------------------------------------------------------
    void GazeSystem::EnableCursor( bool enable )
    {
      m_systemEnabled = enable;

      m_modelEntry->SetVisible( enable );
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

    //----------------------------------------------------------------------------
    void GazeSystem::RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg )
    {
      callbackMap[L"show cursor"] = [this]( SpeechRecognitionResult ^ result )
      {
        EnableCursor( true );
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Cursor on." );
      };

      callbackMap[L"hide cursor"] = [this]( SpeechRecognitionResult ^ result )
      {
        EnableCursor( false );
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Cursor off." );
      };
    }
  }
}