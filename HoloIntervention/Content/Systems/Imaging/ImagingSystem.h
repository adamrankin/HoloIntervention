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
#include "IStabilizedComponent.h"
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
    class NetworkSystem;
    class NotificationSystem;

    class ImagingSystem : public Sound::IVoiceInput, public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      ImagingSystem(RegistrationSystem& registrationSystem,
                    NotificationSystem& notificationSystem,
                    Rendering::SliceRenderer& sliceRenderer,
                    Rendering::VolumeRenderer& volumeRenderer,
                    NetworkSystem& networkSystem,
                    Windows::Storage::StorageFolder^ configStorageFolder);
      ~ImagingSystem();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

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
      NetworkSystem&                        m_networkSystem;
      Rendering::SliceRenderer&             m_sliceRenderer;
      Rendering::VolumeRenderer&            m_volumeRenderer;

      // Common variables
      UWPOpenIGTLink::TransformRepository^  m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();

      // Slice system
      std::wstring                          m_sliceConnectionName;
      std::wstring                          m_sliceFromCoordFrame = L"Image";
      std::wstring                          m_sliceToCoordFrame = L"HMD";
      UWPOpenIGTLink::TransformName^        m_sliceToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_sliceFromCoordFrame.c_str()), ref new Platform::String(m_sliceToCoordFrame.c_str()));
      uint64                                m_sliceToken = INVALID_TOKEN;
      double                                m_lastSliceTimestamp = 0.0;
      std::atomic_bool                      m_sliceValid = false;

      // Volume system
      std::wstring                          m_volumeConnectionName;
      std::wstring                          m_volumeFromCoordFrame = L"Volume";
      std::wstring                          m_volumeToCoordFrame = L"HMD";
      UWPOpenIGTLink::TransformName^        m_volumeToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_volumeFromCoordFrame.c_str()), ref new Platform::String(m_volumeToCoordFrame.c_str()));
      uint64                                m_volumeToken = INVALID_TOKEN;
      double                                m_lastVolumeTimestamp = 0.0;
      std::atomic_bool                      m_volumeValid = false;
    };
  }
}