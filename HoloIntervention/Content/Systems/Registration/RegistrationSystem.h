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

// STL includes
#include <vector>

// Local includes
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// WinRT includes
#include <ppltasks.h>

// Registration method includes
#include "CameraRegistration.h"
#include "NetworkPCLRegistration.h"

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelEntry;
  }

  namespace System
  {
    class RegistrationSystem : public Sound::IVoiceInput, public IEngineComponent
    {
    public:
      RegistrationSystem(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~RegistrationSystem();

      void Update(DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      Concurrency::task<void> LoadAppStateAsync();
      bool IsCameraActive() const;
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks);

      Windows::Foundation::Numerics::float4x4 GetTrackerToCoordinateSystemTransformation(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

    protected:
      // Cached references
      std::shared_ptr<DX::DeviceResources>            m_deviceResources;

      // Anchor variables
      std::atomic_bool                                m_registrationActive = false;
      std::atomic_bool                                m_regAnchorRequested = false;
      uint64_t                                        m_regAnchorModelId = 0;
      std::shared_ptr<Rendering::ModelEntry>          m_regAnchorModel = nullptr;
      Windows::Perception::Spatial::SpatialAnchor^    m_regAnchor = nullptr;
      Windows::Foundation::Numerics::float4x4         m_anchorToCurrentWorld = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4         m_anchorToDesiredWorld = Windows::Foundation::Numerics::float4x4::identity();
      float                                           m_lerpTimer = 0.f;

      // Registration methods
      std::shared_ptr<CameraRegistration>             m_cameraRegistration;
      Windows::Foundation::Numerics::float4x4         m_cachedRegistrationTransform = Windows::Foundation::Numerics::float4x4::identity();

      // Constants
      static Platform::String^                        REGISTRATION_ANCHOR_NAME;
      static const std::wstring                       REGISTRATION_ANCHOR_MODEL_FILENAME;
      static const float                              REGISTRATION_ANCHOR_MODEL_LERP_RATE;
    };
  }
}