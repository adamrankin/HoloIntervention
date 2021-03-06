/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "IConfigurable.h"
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  class Debug;

  namespace Rendering
  {
    class Slice;
    class SliceRenderer;
    class Volume;
    class VolumeRenderer;
  }

  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;

    class ImagingSystem : public Input::IVoiceInput, public IStabilizedComponent, public IConfigurable
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      ImagingSystem(HoloInterventionCore& core,
                    RegistrationSystem& registrationSystem,
                    NotificationSystem& notificationSystem,
                    Rendering::SliceRenderer& sliceRenderer,
                    Rendering::VolumeRenderer& volumeRenderer,
                    NetworkSystem& networkSystem,
                    Debug& debug);
      ~ImagingSystem();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

      bool HasSlice() const;
      Windows::Foundation::Numerics::float4x4 GetSlicePose() const;
      Windows::Foundation::Numerics::float3 GetSliceVelocity() const;

      bool HasVolume() const;

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);

    protected:
      void Process2DFrame(UWPOpenIGTLink::VideoFrame^ frame, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);
      void Process3DFrame(UWPOpenIGTLink::VideoFrame^ frame, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

    protected:
      // Cached variables
      NotificationSystem&                     m_notificationSystem;
      RegistrationSystem&                     m_registrationSystem;
      NetworkSystem&                          m_networkSystem;
      Rendering::SliceRenderer&               m_sliceRenderer;
      Rendering::VolumeRenderer&              m_volumeRenderer;
      Debug&                                  m_debug;

      // Common variables
      UWPOpenIGTLink::TransformRepository^    m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();

      // Slice system
      std::wstring                            m_sliceConnectionName; // For saving back to disk
      uint64                                  m_hashedSliceConnectionName;
      std::wstring                            m_sliceFromCoordFrame = L"Image";
      std::wstring                            m_sliceToCoordFrame = HOLOLENS_COORDINATE_SYSTEM_WNAME;
      UWPOpenIGTLink::TransformName^          m_sliceToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_sliceFromCoordFrame.c_str()), ref new Platform::String(m_sliceToCoordFrame.c_str()));
      std::shared_ptr<Rendering::Slice>  m_sliceEntry = nullptr;

      double                                  m_latestSliceTimestamp = 0.0;
      Windows::Foundation::Numerics::float4   m_whiteMapColour = { 1.f, 1.f, 1.f, 1.f };
      Windows::Foundation::Numerics::float4   m_blackMapColour = { 0.f, 0.f, 0.f, 1.f };

      // Volume system
      std::wstring                            m_volumeConnectionName; // For saving back to disk
      uint64                                  m_hashedVolumeConnectionName;
      std::wstring                            m_volumeFromCoordFrame = L"Volume";
      std::wstring                            m_volumeToCoordFrame = HOLOLENS_COORDINATE_SYSTEM_WNAME;
      UWPOpenIGTLink::TransformName^          m_volumeToHMDName = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(m_volumeFromCoordFrame.c_str()), ref new Platform::String(m_volumeToCoordFrame.c_str()));
      std::shared_ptr<Rendering::Volume> m_volumeEntry = nullptr;
      double                                  m_latestVolumeTimestamp = 0.0;
    };
  }
}