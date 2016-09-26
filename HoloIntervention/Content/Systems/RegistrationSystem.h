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

// std includes
#include <vector>

// Sound includes
#include "IVoiceInput.h"

// WinRT includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelEntry;
  }

  namespace System
  {
    class RegistrationSystem : public Sound::IVoiceInput
    {
    public:
      RegistrationSystem( const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer );
      ~RegistrationSystem();

      void Update( SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose );

      task<void> LoadAppStateAsync();

      virtual void RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg );

    protected:
      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>      m_deviceResources;
      DX::StepTimer&                            m_stepTimer;

      bool                                      m_regAnchorRequested = false;
      uint64_t                                  m_regAnchorModelId = 0;
      std::shared_ptr<Rendering::ModelEntry>    m_regAnchorModel = nullptr;

      static const std::wstring                 ANCHOR_MODEL_FILENAME;
    };
  }
}