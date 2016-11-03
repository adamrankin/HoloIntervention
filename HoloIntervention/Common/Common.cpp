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
#include "Common.h"

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  task<void> complete_after(unsigned int timeoutMs)
  {
    // A task completion event that is set when a timer fires.
    task_completion_event<void> tce;

    // Create a non-repeating timer.
    auto fire_once = new timer<int>(timeoutMs, 0, nullptr, false);
    // Create a call object that sets the completion event after the timer fires.
    auto callback = new call<int>([tce](int)
    {
      tce.set();
    });

    // Connect the timer to the callback and start the timer.
    fire_once->link_target(callback);
    fire_once->start();

    // Create a task that completes after the completion event is set.
    task<void> event_set(tce);

    // Create a continuation task that cleans up resources and
    // and return that continuation task.
    return event_set.then([callback, fire_once]()
    {
      delete callback;
      delete fire_once;
    });
  }

  //----------------------------------------------------------------------------
  void MillimetersToMeters(float4x4& transform)
  {
    transform = transform * make_float4x4_scale(0.001f);
  }

  //----------------------------------------------------------------------------
  int IsLittleEndian()
  {
    short a = 1;
    return ((char*)&a)[0];
  }
}