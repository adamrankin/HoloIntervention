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
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// Rendering includes
#include "SliceRenderer.h"
#include "VolumeRenderer.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class SliceRenderer;
    class VolumeRenderer;
  }

  namespace System
  {
    class NotificationSystem;

    class ImagingSystem : public Sound::IVoiceInput, public IEngineComponent
    {
    public:
      ImagingSystem(RegistrationSystem& registrationSystem, NotificationSystem& notificationSystem, Rendering::SliceRenderer& sliceRenderer, Rendering::VolumeRenderer& volumeRenderer);
      ~ImagingSystem();

      void Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

      bool HasSlice() const;
      Windows::Foundation::Numerics::float4x4 GetSlicePose() const;
      Windows::Foundation::Numerics::float3 GetSliceVelocity() const;

      bool HasVolume() const;

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      void Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);
      void Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

    protected:
      // Cached variables
      NotificationSystem&                   m_notificationSystem;
      RegistrationSystem&                   m_registrationSystem;
      Rendering::SliceRenderer&             m_sliceRenderer;
      Rendering::VolumeRenderer&            m_volumeRenderer;

      // Slice system
      std::wstring                          m_fromCoordFrame = L"Image";
      std::wstring                          m_toCoordFrame = L"HMD";
      UWPOpenIGTLink::TransformName^        m_imageToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_fromCoordFrame.c_str()), ref new Platform::String(m_toCoordFrame.c_str()));
      UWPOpenIGTLink::TransformRepository^  m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      uint64                                m_sliceToken = INVALID_TOKEN;

      // Volume system
      uint64                                m_volumeToken = INVALID_TOKEN;
    };
  }
}