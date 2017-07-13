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

#include "pch.h"
#include "PhantomTask.h"

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    namespace Tasks
    {
      //----------------------------------------------------------------------------
      task<bool> PhantomTask::WriteConfigurationAsync(XmlDocument^ document)
      {
        return task_from_result(true);
      }

      //----------------------------------------------------------------------------
      task<bool> PhantomTask::ReadConfigurationAsync(XmlDocument^ document)
      {
        return task_from_result(true);
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedPosition(SpatialPointerPose^ pose) const
      {
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float3 PhantomTask::GetStabilizedVelocity() const
      {
        return float3(0.f, 0.f, 0.f);
      }

      //----------------------------------------------------------------------------
      float PhantomTask::GetStabilizePriority() const
      {
        return m_componentReady ? PRIORITY_PHANTOM_TASK : PRIORITY_NOT_ACTIVE;
      }

      //----------------------------------------------------------------------------
      void PhantomTask::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
      {

      }

      //----------------------------------------------------------------------------
      PhantomTask::PhantomTask()
      {
      }

      //----------------------------------------------------------------------------
      PhantomTask::~PhantomTask()
      {
      }
    }
  }
}