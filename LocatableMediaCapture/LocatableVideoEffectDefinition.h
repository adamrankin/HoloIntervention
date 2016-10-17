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

// stl includes
#include <algorithm>

namespace HoloIntervention
{
  namespace LocatableMediaCapture
  {
    template<typename T, typename U>
    U GetValueFromPropertySet(Windows::Foundation::Collections::IPropertySet^ propertySet, Platform::String^ key, U defaultValue)
    {
      try
      {
        return static_cast<U>(safe_cast<T>(propertySet->Lookup(key)));
      }
      catch (Platform::OutOfBoundsException^)
      {
        // The key is not present in the PropertySet. Return the default value.
        return defaultValue;
      }
    }

    // This class provides an IAudioEffectDefinition which can be used
    // to configure and create a MixedRealityCaptureVideoEffect
    // object. See https://developer.microsoft.com/en-us/windows/holographic/mixed_reality_capture_for_developers#creating_a_custom_mixed_reality_capture_.28mrc.29_recorder
    // for more information about the effect definition properties.

#define RUNTIMECLASS_MIXEDREALITYCAPTURE_VIDEO_EFFECT L"Windows.Media.MixedRealityCapture.MixedRealityCaptureVideoEffect"

    //
    // StreamType: Describe which capture stream this effect is used for.
    // Type: Windows::Media::Capture::MediaStreamType as UINT32
    // Default: VideoRecord
    //
#define PROPERTY_STREAMTYPE L"StreamType"

    //
    // HologramCompositionEnabled: Flag to enable or disable holograms in video capture.
    // Type: bool
    // Default: True
    //
#define PROPERTY_HOLOGRAMCOMPOSITIONENABLED L"HologramCompositionEnabled"

    //
    // RecordingIndicatorEnabled: Flag to enable or disable recording indicator on screen during hologram capturing.
    // Type: bool
    // Default: True
    //
#define PROPERTY_RECORDINGINDICATORENABLED L"RecordingIndicatorEnabled"

    //
    // VideoStabilizationEnabled: Flag to enable or disable video stabilization powered by the HoloLens tracker.
    // Type : bool
    // Default: False
    //
#define PROPERTY_VIDEOSTABILIZATIONENABLED L"VideoStabilizationEnabled"

    //
    // VideoStabilizationBufferLength: Set how many historical frames are used for video stabilization.
    // Type : UINT32 (Max num is 30)
    // Default: 0
    //
#define PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH L"VideoStabilizationBufferLength"

    //
    // GlobalOpacityCoefficient: Set global opacity coefficient of hologram.
    // Type : float (0.0 to 1.0)
    // Default: 0.9
    //
#define PROPERTY_GLOBALOPACITYCOEFFICIENT L"GlobalOpacityCoefficient"

    //
    // Maximum value of VideoStabilizationBufferLength
    // This number is defined and used in MixedRealityCaptureVideoEffect
    //
#define PROPERTY_MAX_VSBUFFER 30U

    public ref class LocatableVideoEffectDefinition sealed : public Windows::Media::Effects::IVideoEffectDefinition
    {
    public:
      LocatableVideoEffectDefinition();

      //
      // IVideoEffectDefinition
      //
      property Platform::String ^ ActivatableClassId
      {
        virtual Platform::String ^ get()
        {
          return m_activatableClassId;
        }
      }

      property Windows::Foundation::Collections::IPropertySet^ Properties
      {
        virtual Windows::Foundation::Collections::IPropertySet ^ get()
        {
          return m_propertySet;
        }
      }

      //
      // Mixed Reality Capture effect properties
      //
      property Windows::Media::Capture::MediaStreamType StreamType
      {
        Windows::Media::Capture::MediaStreamType get()
        {
          return GetValueFromPropertySet<uint32_t>(m_propertySet, PROPERTY_STREAMTYPE, DefaultStreamType);
        }

        void set(Windows::Media::Capture::MediaStreamType newValue)
        {
          m_propertySet->Insert(PROPERTY_STREAMTYPE, static_cast<uint32_t>(newValue));
        }
      }

      property bool HologramCompositionEnabled
      {
        bool get()
        {
          return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_HOLOGRAMCOMPOSITIONENABLED, DefaultHologramCompositionEnabled);
        }

        void set(bool newValue)
        {
          m_propertySet->Insert(PROPERTY_HOLOGRAMCOMPOSITIONENABLED, newValue);
        }
      }

      property bool RecordingIndicatorEnabled
      {
        bool get()
        {
          return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_RECORDINGINDICATORENABLED, DefaultRecordingIndicatorEnabled);
        }

        void set(bool newValue)
        {
          m_propertySet->Insert(PROPERTY_RECORDINGINDICATORENABLED, newValue);
        }
      }

      property bool VideoStabilizationEnabled
      {
        bool get()
        {
          return GetValueFromPropertySet<bool>(m_propertySet, PROPERTY_VIDEOSTABILIZATIONENABLED, DefaultVideoStabilizationEnabled);
        }

        void set(bool newValue)
        {
          m_propertySet->Insert(PROPERTY_VIDEOSTABILIZATIONENABLED, newValue);
        }
      }

      property uint32_t VideoStabilizationBufferLength
      {
        uint32_t get()
        {
          return GetValueFromPropertySet<uint32_t>(m_propertySet, PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH, DefaultVideoStabilizationBufferLength);
        }

        void set(uint32_t newValue)
        {
          m_propertySet->Insert(PROPERTY_VIDEOSTABILIZATIONBUFFERLENGTH, (std::min)(newValue, PROPERTY_MAX_VSBUFFER));
        }
      }

      property float GlobalOpacityCoefficient
      {
        float get()
        {
          return GetValueFromPropertySet<float>(m_propertySet, PROPERTY_GLOBALOPACITYCOEFFICIENT, DefaultGlobalOpacityCoefficient);
        }

        void set(float newValue)
        {
          m_propertySet->Insert(PROPERTY_GLOBALOPACITYCOEFFICIENT, newValue);
        }
      }


      property uint32_t VideoStabilizationMaximumBufferLength
      {
        uint32_t get()
        {
          return PROPERTY_MAX_VSBUFFER;
        }
      }

    private:
      static constexpr Windows::Media::Capture::MediaStreamType DefaultStreamType = Windows::Media::Capture::MediaStreamType::VideoRecord;
      static constexpr bool DefaultHologramCompositionEnabled = true;
      static constexpr bool DefaultRecordingIndicatorEnabled = true;
      static constexpr bool DefaultVideoStabilizationEnabled = false;
      static constexpr uint32_t DefaultVideoStabilizationBufferLength = 0U;
      static constexpr float DefaultGlobalOpacityCoefficient = 0.9f;
    private:
      Platform::String^ m_activatableClassId = ref new Platform::String(RUNTIMECLASS_MIXEDREALITYCAPTURE_VIDEO_EFFECT);
      Windows::Foundation::Collections::PropertySet^ m_propertySet = ref new Windows::Foundation::Collections::PropertySet();
    };
  }
}