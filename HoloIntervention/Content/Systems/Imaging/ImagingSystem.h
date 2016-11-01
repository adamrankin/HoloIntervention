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
#include "IVoiceInput.h"

// Rendering includes
#include "SliceRenderer.h"

using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace System
  {
    class ImagingSystem : public Sound::IVoiceInput
    {
    public:
      ImagingSystem();
      ~ImagingSystem();

      void Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer);

      bool HasSlice() const;
      float4x4 GetSlicePose() const;

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      void Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame);
      void Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame);

    protected:
      // Slice system
      uint32                    m_sliceToken = Rendering::SliceRenderer::INVALID_SLICE_INDEX;
    };
  }
}