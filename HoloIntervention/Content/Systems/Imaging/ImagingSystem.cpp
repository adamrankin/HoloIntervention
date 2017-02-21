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
#include "AppView.h"
#include "Common.h"
#include "ImagingSystem.h"
#include "StepTimer.h"

// Network includes
#include "IGTConnector.h"

// System includes
#include "NetworkSystem.h"
#include "NotificationSystem.h"
#include "RegistrationSystem.h"

// Rendering includes
#include "VolumeRenderer.h"
#include "SliceRenderer.h"

// Unnecessary, but removes intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ImagingSystem::GetStabilizedPosition() const
    {
      std::shared_ptr<Rendering::SliceEntry> sliceEntry(nullptr);
      if (m_sliceValid)
      {
        sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);
      }

      std::shared_ptr<Rendering::VolumeEntry> volumeEntry(nullptr);
      if (m_volumeValid)
      {
        volumeEntry = m_volumeRenderer.GetVolume(m_volumeToken);
      }

      if (m_sliceValid && m_volumeValid)
      {
        // TODO : which one is close to the view frustrum?
        // TODO : which one is more recent?
        // TODO : other metrics?
        return m_lastSliceTimestamp > m_lastVolumeTimestamp ? transform(float3(0.f, 0.f, 0.f), sliceEntry->GetCurrentPose()) : transform(float3(0.f, 0.f, 0.f), volumeEntry->GetCurrentPose());
      }
      else if (m_volumeValid)
      {
        return transform(float3(0.f, 0.f, 0.f), volumeEntry->GetCurrentPose());
      }
      else if (m_sliceValid)
      {
        return transform(float3(0.f, 0.f, 0.f), sliceEntry->GetCurrentPose());
      }
      else
      {
        return float3(0.f, 0.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ImagingSystem::GetStabilizedNormal() const
    {
      std::shared_ptr<Rendering::SliceEntry> sliceEntry(nullptr);
      if (m_sliceValid)
      {
        sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);
      }

      std::shared_ptr<Rendering::VolumeEntry> volumeEntry(nullptr);
      if (m_volumeValid)
      {
        volumeEntry = m_volumeRenderer.GetVolume(m_volumeToken);
      }

      if (m_sliceValid && m_volumeValid)
      {
        // TODO : which one is close to the view frustrum?
        // TODO : which one is more recent?
        // TODO : other metrics?
        return m_lastSliceTimestamp > m_lastVolumeTimestamp ? ExtractNormal(sliceEntry->GetCurrentPose()) : ExtractNormal(volumeEntry->GetCurrentPose());
      }
      else if (m_volumeValid)
      {
        return ExtractNormal(volumeEntry->GetCurrentPose());
      }
      else if (m_sliceValid)
      {
        return ExtractNormal(sliceEntry->GetCurrentPose());
      }
      else
      {
        return float3(0.f, 1.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ImagingSystem::GetStabilizedVelocity() const
    {
      std::shared_ptr<Rendering::SliceEntry> sliceEntry(nullptr);
      if (m_sliceValid)
      {
        sliceEntry = m_sliceRenderer.GetSlice(m_sliceToken);
      }

      std::shared_ptr<Rendering::VolumeEntry> volumeEntry(nullptr);
      if (m_volumeValid)
      {
        volumeEntry = m_volumeRenderer.GetVolume(m_volumeToken);
      }

      if (m_sliceValid && m_volumeValid)
      {
        // TODO : which one is close to the view frustrum?
        // TODO : which one is more recent?
        // TODO : other metrics?
        return m_lastSliceTimestamp > m_lastVolumeTimestamp ? sliceEntry->GetStabilizedVelocity() : volumeEntry->GetVelocity();
      }
      else if (m_volumeValid)
      {
        return volumeEntry->GetVelocity();
      }
      else if (m_sliceValid)
      {
        return sliceEntry->GetStabilizedVelocity();
      }
      else
      {
        return float3(0.f, 0.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    float ImagingSystem::GetStabilizePriority() const
    {
      // TODO : are they in frustum?

      if (m_sliceValid || m_volumeValid)
      {
        // TODO : stabilizer values?
        return 3.f;
      }

      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    ImagingSystem::ImagingSystem(RegistrationSystem& registrationSystem, NotificationSystem& notificationSystem, Rendering::SliceRenderer& sliceRenderer, Rendering::VolumeRenderer& volumeRenderer, NetworkSystem& networkSystem, StorageFolder^ configStorageFolder)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_sliceRenderer(sliceRenderer)
      , m_volumeRenderer(volumeRenderer)
      , m_networkSystem(networkSystem)
    {
      try
      {
        InitializeTransformRepositoryAsync(L"configuration.xml", configStorageFolder, m_transformRepository).then([this]()
        {
          m_componentReady = true;
        });
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
      }

      try
      {
        LoadXmlDocumentAsync(L"configuration.xml", configStorageFolder).then([this](XmlDocument ^ doc)
        {
          auto fromToFunction = [this, doc](Platform::String ^ xpath, std::wstring & m_from, std::wstring & m_to, UWPOpenIGTLink::TransformName^& name, std::wstring & connectionName)
          {
            if (doc->SelectNodes(xpath)->Length != 1)
            {
              // No configuration found, use defaults
              return;
            }

            IXmlNode^ node = doc->SelectNodes(xpath)->Item(0);
            Platform::String^ fromAttribute = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
            Platform::String^ toAttribute = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
            if (fromAttribute->IsEmpty() || toAttribute->IsEmpty())
            {
              return;
            }
            else
            {
              m_from = std::wstring(fromAttribute->Data());
              m_to = std::wstring(toAttribute->Data());
              name = ref new UWPOpenIGTLink::TransformName(fromAttribute, toAttribute);
            }

            Platform::String^ igtConnection = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
            if (igtConnection != nullptr)
            {
              connectionName = std::wstring(begin(igtConnection), end(igtConnection));
            }
          };

          fromToFunction(L"/HoloIntervention/VolumeRendering", m_volumeFromCoordFrame, m_volumeToCoordFrame, m_volumeToHMDName, m_volumeConnectionName);
          fromToFunction(L"/HoloIntervention/SliceRendering", m_sliceFromCoordFrame, m_sliceToCoordFrame, m_sliceToHMDName, m_sliceConnectionName);
        });
      }
      catch (Platform::Exception^ e)
      {
        HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, e->Message);
      }
    }

    //----------------------------------------------------------------------------
    ImagingSystem::~ImagingSystem()
    {
      m_componentReady = false;
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ coordSystem)
    {
      UWPOpenIGTLink::TrackedFrame^ frame(nullptr);
      std::shared_ptr<Network::IGTConnector> sliceConnection = m_networkSystem.GetConnection(m_sliceConnectionName);
      if (sliceConnection == nullptr)
      {
        return;
      }
      if (sliceConnection->GetTrackedFrame(frame, &m_lastSliceTimestamp))
      {
        if (frame->HasImage())
        {
          if (frame->Dimensions[2] == 1)
          {
            m_transformRepository->SetTransforms(frame);
            Process2DFrame(frame, coordSystem);
          }
        }
      }

      std::shared_ptr<Network::IGTConnector> volumeConnection = m_networkSystem.GetConnection(m_volumeConnectionName);
      if (volumeConnection == nullptr)
      {
        return;
      }
      if (volumeConnection->GetTrackedFrame(frame, &m_lastVolumeTimestamp))
      {
        if (frame->HasImage())
        {
          if (frame->Dimensions[2] > 1)
          {
            m_transformRepository->SetTransforms(frame);
            Process3DFrame(frame, coordSystem);
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    bool ImagingSystem::HasSlice() const
    {
      return m_sliceToken != INVALID_TOKEN;
    }

    //----------------------------------------------------------------------------
    float4x4 ImagingSystem::GetSlicePose() const
    {
      try
      {
        return m_sliceRenderer.GetSlicePose(m_sliceToken);
      }
      catch (const std::exception&)
      {
        throw std::exception("Unable to retrieve slice pose.");
      }
    }

    //----------------------------------------------------------------------------
    float3 ImagingSystem::GetSliceVelocity() const
    {
      try
      {
        return m_sliceRenderer.GetSliceVelocity(m_sliceToken);
      }
      catch (const std::exception&)
      {
        throw std::exception("Unable to retrieve slice velocity.");
      }
    }

    //----------------------------------------------------------------------------
    bool ImagingSystem::HasVolume() const
    {
      return m_volumeToken != INVALID_TOKEN;
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"slice on"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HasSlice())
        {
          m_notificationSystem.QueueMessage(L"Slice showing.");
          m_sliceRenderer.ShowSlice(m_sliceToken);
          return;
        }
        m_notificationSystem.QueueMessage(L"No slice available.");
      };

      callbackMap[L"slice off"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HasSlice())
        {
          m_notificationSystem.QueueMessage(L"Slice hidden.");
          m_sliceRenderer.HideSlice(m_sliceToken);
          return;
        }
        m_notificationSystem.QueueMessage(L"No slice available.");
      };

      callbackMap[L"lock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!HasSlice())
        {
          m_notificationSystem.QueueMessage(L"No slice to head-lock!");
          return;
        }
        m_notificationSystem.QueueMessage(L"Slice is now head-locked.");
        m_sliceRenderer.SetSliceHeadlocked(m_sliceToken, true);
      };

      callbackMap[L"unlock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!HasSlice())
        {
          m_notificationSystem.QueueMessage(L"No slice to unlock!");
          return;
        }
        m_notificationSystem.QueueMessage(L"Slice is now in world-space.");
        m_sliceRenderer.SetSliceHeadlocked(m_sliceToken, false);
      };

      callbackMap[L"piecewise linear transfer function"] = [this](SpeechRecognitionResult ^ result)
      {
        m_notificationSystem.QueueMessage(L"Using built-in piecewise linear transfer function.");
        // TODO : how to define which volume to apply to?
      };
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      m_lastSliceTimestamp = frame->Timestamp;

      // Update the transform repository with the latest registration
      float4x4 referenceToHMD(float4x4::identity());
      if (!m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD))
      {
        return;
      }

      if (!m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(referenceToHMD), true))
      {
        return;
      }

      float4x4 sliceToHMD(float4x4::identity());
      try
      {
        sliceToHMD = m_transformRepository->GetTransform(m_sliceToHMDName);
      }
      catch (Platform::Exception^)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, L"Unable to retrieve " + m_sliceToHMDName->GetTransformName() + L" from repository.");
        return;
      }

      if (!HasSlice())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_sliceToken = m_sliceRenderer.AddSlice(frame, sliceToHMD);
      }
      else
      {
        m_sliceRenderer.UpdateSlice(m_sliceToken, frame, sliceToHMD);
      }
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      m_lastVolumeTimestamp = frame->Timestamp;

      // Update the transform repository with the latest registration
      float4x4 referenceToHMD(float4x4::identity());
      if (!m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD))
      {
        return;
      }

      if (!m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(referenceToHMD), true))
      {
        return;
      }

      float4x4 volumeToHMD(float4x4::identity());
      try
      {
        volumeToHMD = m_transformRepository->GetTransform(m_volumeToHMDName);
      }
      catch (Platform::Exception^)
      {
        Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, L"Unable to retrieve " + m_volumeToHMDName->GetTransformName() + L" from repository.");
        return;
      }

      if (!HasVolume())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_volumeToken = m_volumeRenderer.AddVolume(frame, volumeToHMD);
      }
      else
      {
        m_volumeRenderer.UpdateVolume(m_volumeToken, frame, volumeToHMD);
      }
    }
  }
}