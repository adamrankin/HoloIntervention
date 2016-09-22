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
#include "ISystem.h"

// Model includes
#include "ModelEntry.h"

using namespace Windows::Foundation::Numerics;

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace System
  {
    class GazeSystem : public ISystem
    {
    public:
      GazeSystem();
      ~GazeSystem();

      void Update( const DX::StepTimer& timer, SpatialCoordinateSystem^ currentCoordinateSystem, SpatialPointerPose^ headPose);

      void EnableCursor( bool enable );
      bool IsCursorEnabled();

      const float3& GetHitPosition() const;
      const float3& GetHitNormal() const;

      // ISystem functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Input::VoiceInputCallbackMap& callbackMap);

    protected:
      std::shared_ptr<Rendering::ModelEntry>    m_modelEntry;
      uint64                                    m_modelToken;
      bool                                      m_systemEnabled;
      float3                                    m_lastHitPosition;
      float3                                    m_lastHitNormal;

      static const std::wstring GAZE_CURSOR_ASSET_LOCATION;
      static const uint32 FRAMES_UNTIL_HIT_EXPIRES;
    };
  }
}