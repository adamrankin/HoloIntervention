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

// Local includes
#include "pch.h"
#include "Common.h"
#include "Debug.h"
#include "ImagingSystem.h"
#include "StepTimer.h"

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

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Collections;
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
    float3 ImagingSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      if (m_sliceEntry != nullptr && m_volumeEntry != nullptr)
      {
        // TODO : which one is close to the view frustum?
        // TODO : which one is more recent?
        // TODO : other metrics?
        return m_latestSliceTimestamp > m_latestVolumeTimestamp ? transform(float3(0.f, 0.f, 0.f), m_sliceEntry->GetCurrentPose()) : transform(float3(0.f, 0.f, 0.f), m_volumeEntry->GetCurrentPose());
      }
      else if (m_volumeEntry != nullptr)
      {
        return transform(float3(0.f, 0.f, 0.f), m_volumeEntry->GetCurrentPose());
      }
      else if (m_sliceEntry != nullptr)
      {
        return transform(float3(0.f, 0.f, 0.f), m_sliceEntry->GetCurrentPose());
      }
      else
      {
        return float3(0.f, 0.f, 0.f);
      }
    }

    //----------------------------------------------------------------------------
    float3 ImagingSystem::GetStabilizedVelocity() const
    {
      if (m_sliceEntry != nullptr && m_volumeEntry != nullptr)
      {
        // TODO : which one is close to the view frustum?
        // TODO : which one is more recent?
        // TODO : other metrics?
        return m_latestSliceTimestamp > m_latestVolumeTimestamp ? m_sliceEntry->GetStabilizedVelocity() : m_volumeEntry->GetVelocity();
      }
      else if (m_volumeEntry != nullptr)
      {
        return m_volumeEntry->GetVelocity();
      }
      else if (m_sliceEntry != nullptr)
      {
        return m_sliceEntry->GetStabilizedVelocity();
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
      if ((m_sliceEntry != nullptr && m_sliceEntry->IsValid()) || (m_volumeEntry != nullptr && m_volumeEntry->IsValid()))
      {
        return PRIORITY_IMAGING;
      }

      return PRIORITY_NOT_ACTIVE;
    }

    //----------------------------------------------------------------------------
    task<bool> ImagingSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention");
        if (document->SelectNodes(xpath)->Length == 0)
        {
          XmlElement^ rootElem = document->CreateElement(L"HoloIntervention");
          document->AppendChild(rootElem);
        }

        m_transformRepository->WriteConfiguration(document);

        auto rootNode = document->SelectNodes(xpath)->Item(0);

        auto sliceElem = document->CreateElement("SliceRendering");
        sliceElem->SetAttribute(L"From", ref new Platform::String(m_sliceFromCoordFrame.c_str()));
        sliceElem->SetAttribute(L"To", ref new Platform::String(m_sliceToCoordFrame.c_str()));
        sliceElem->SetAttribute(L"IGTConnection", ref new Platform::String(m_sliceConnectionName.c_str()));
        {
          std::wstringstream ss;
          ss << m_whiteMapColour.x << " " << m_whiteMapColour.y << " " << m_whiteMapColour.z << " " << m_whiteMapColour.w;
          sliceElem->SetAttribute(L"WhiteMapColour", ref new Platform::String(ss.str().c_str()));
        }
        {
          std::wstringstream ss;
          ss << std::fixed << m_blackMapColour.x << " " << m_blackMapColour.y << " " << m_blackMapColour.z << " " << m_blackMapColour.w;
          sliceElem->SetAttribute(L"BlackMapColour", ref new Platform::String(ss.str().c_str()));
        }
        rootNode->AppendChild(sliceElem);

        auto volumeElem = document->CreateElement("VolumeRendering");
        volumeElem->SetAttribute(L"From", ref new Platform::String(m_volumeFromCoordFrame.c_str()));
        volumeElem->SetAttribute(L"To", ref new Platform::String(m_volumeToCoordFrame.c_str()));
        volumeElem->SetAttribute(L"IGTConnection", ref new Platform::String(m_volumeConnectionName.c_str()));
        rootNode->AppendChild(volumeElem);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> ImagingSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (!m_transformRepository->ReadConfiguration(document))
        {
          return false;
        }

        auto fromToFunction = [this, document](Platform::String ^ xpath, std::wstring & m_from, std::wstring & m_to, UWPOpenIGTLink::TransformName^& name, uint64 & hashedConnectionName, std::wstring & connectionName)
        {
          if (document->SelectNodes(xpath)->Length != 1)
          {
            // No configuration found, use defaults
            return;
          }

          auto node = document->SelectNodes(xpath)->Item(0);
          auto fromAttribute = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"From")->NodeValue);
          auto toAttribute = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"To")->NodeValue);
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

          Platform::String^ igtConnection = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
          if (igtConnection != nullptr)
          {
            connectionName = std::wstring(igtConnection->Data());
            hashedConnectionName = HashString(igtConnection);
          }
        };

        fromToFunction(L"/HoloIntervention/VolumeRendering", m_volumeFromCoordFrame, m_volumeToCoordFrame, m_volumeToHMDName, m_hashedVolumeConnectionName, m_volumeConnectionName);
        fromToFunction(L"/HoloIntervention/SliceRendering", m_sliceFromCoordFrame, m_sliceToCoordFrame, m_sliceToHMDName, m_hashedSliceConnectionName, m_sliceConnectionName);

        if (document->SelectNodes(L"/HoloIntervention/SliceRendering")->Length == 1)
        {
          auto node = document->SelectNodes(L"/HoloIntervention/SliceRendering")->Item(0);
          auto whiteMapColourString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"WhiteMapColour")->NodeValue);
          auto blackMapColourString = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"BlackMapColour")->NodeValue);
          {
            std::wstringstream ss;
            ss << whiteMapColourString->Data();
            ss >> m_whiteMapColour.x >> m_whiteMapColour.y >> m_whiteMapColour.z >> m_whiteMapColour.w;
          }
          {
            std::wstringstream ss;
            ss << whiteMapColourString->Data();
            ss >> m_blackMapColour.x >> m_blackMapColour.y >> m_blackMapColour.z >> m_blackMapColour.w;
          }
        }

        m_componentReady = true;

        return true;
      });
    }

    //----------------------------------------------------------------------------
    ImagingSystem::ImagingSystem(RegistrationSystem& registrationSystem, NotificationSystem& notificationSystem, Rendering::SliceRenderer& sliceRenderer, Rendering::VolumeRenderer& volumeRenderer, NetworkSystem& networkSystem, Debug& debug)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_sliceRenderer(sliceRenderer)
      , m_volumeRenderer(volumeRenderer)
      , m_networkSystem(networkSystem)
      , m_debug(debug)
    {
      m_sliceRenderer.AddSliceAsync(nullptr).then([this](uint64 entryId)
      {
        m_sliceEntry = m_sliceRenderer.GetSlice(entryId);
        m_sliceEntry->SetColorizeGreyscale(true);
        m_sliceEntry->SetWhiteMapColour(m_whiteMapColour);
        m_sliceEntry->SetBlackMapColour(m_blackMapColour);
      });

      m_volumeRenderer.AddVolumeAsync(nullptr).then([this](uint64 entryId)
      {
        m_volumeEntry = m_volumeRenderer.GetVolume(entryId);
      });
    }

    //----------------------------------------------------------------------------
    ImagingSystem::~ImagingSystem()
    {
      m_componentReady = false;
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ coordSystem)
    {
      UWPOpenIGTLink::VideoFrame^ image(nullptr);
      UWPOpenIGTLink::TrackedFrame^ frame = m_networkSystem.GetTrackedFrame(m_hashedSliceConnectionName, m_latestSliceTimestamp);
      if (frame == nullptr)
      {
        image = m_networkSystem.GetImage(m_hashedSliceConnectionName, m_latestSliceTimestamp);
      }
      else
      {
        image = frame->Frame;
      }
      if (image != nullptr && image->Dimensions[2] == 1)
      {
        if (frame != nullptr)
        {
          m_transformRepository->SetTransforms(frame);
        }
        else if (image != nullptr)
        {
          m_transformRepository->SetImageTransform(image);
        }
        Process2DFrame(image, coordSystem);
      }

      frame = m_networkSystem.GetTrackedFrame(m_hashedVolumeConnectionName, m_latestVolumeTimestamp);
      if (frame == nullptr)
      {
        image = m_networkSystem.GetImage(m_hashedVolumeConnectionName, m_latestVolumeTimestamp);
      }
      else
      {
        image = frame->Frame;
      }
      if (image != nullptr && image->Dimensions[2] > 1)
      {
        if (frame != nullptr)
        {
          m_transformRepository->SetTransforms(frame);
        }
        else if (image != nullptr)
        {
          m_transformRepository->SetImageTransform(image);
        }
        Process3DFrame(image, coordSystem);
      }
    }

    //----------------------------------------------------------------------------
    bool ImagingSystem::HasSlice() const
    {
      return m_sliceEntry != nullptr;
    }

    //----------------------------------------------------------------------------
    float4x4 ImagingSystem::GetSlicePose() const
    {
      return m_sliceEntry->GetCurrentPose();
    }

    //----------------------------------------------------------------------------
    float3 ImagingSystem::GetSliceVelocity() const
    {
      return m_sliceEntry->GetStabilizedVelocity();
    }

    //----------------------------------------------------------------------------
    bool ImagingSystem::HasVolume() const
    {
      return m_volumeEntry != nullptr;
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"slice on"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HasSlice())
        {
          m_notificationSystem.QueueMessage(L"Slice showing.");
          m_sliceEntry->SetVisible(true);
          return;
        }
        m_notificationSystem.QueueMessage(L"No slice available.");
      };

      callbackMap[L"slice off"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HasSlice())
        {
          m_notificationSystem.QueueMessage(L"Slice hidden.");
          m_sliceEntry->SetVisible(false);
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
        m_sliceEntry->SetHeadlocked(true, true);
      };

      callbackMap[L"unlock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!HasSlice())
        {
          m_notificationSystem.QueueMessage(L"No slice to unlock!");
          return;
        }
        m_notificationSystem.QueueMessage(L"Slice is now in world-space.");
        m_sliceEntry->SetHeadlocked(false, true);
      };

      callbackMap[L"piecewise linear transfer function"] = [this](SpeechRecognitionResult ^ result)
      {
        m_notificationSystem.QueueMessage(L"Using built-in piecewise linear transfer function.");
        // TODO : how to define which volume to apply to?
      };
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process2DFrame(UWPOpenIGTLink::VideoFrame^ frame, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      m_latestSliceTimestamp = frame->Timestamp;

      if (m_sliceEntry == nullptr)
      {
        // TODO : what happened if this is hit?
        return;
      }

      // Update the transform repository with the latest registration
      if (!m_sliceEntry->GetHeadlocked())
      {
        float4x4 referenceToHMD(float4x4::identity());
        if (!m_registrationSystem.GetReferenceToCoordinateSystemTransformation(hmdCoordinateSystem, referenceToHMD))
        {
          return;
        }

        if (!m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"), transpose(referenceToHMD), true))
        {
          return;
        }

        IKeyValuePair<bool, float4x4>^ result = m_transformRepository->GetTransform(m_sliceToHMDName);
        if (!result->Key)
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to retrieve " + m_sliceToHMDName->GetTransformName() + L" from repository.");
          return;
        }
        float4x4 imageToHMDTransform = transpose(result->Value);

#if defined(_DEBUG)
        auto name = ref new UWPOpenIGTLink::TransformName(L"Probe", L"Reference");
        double latestTimestamp(0.0);
        UWPOpenIGTLink::Transform^ transform = m_networkSystem.GetTransform(m_hashedSliceConnectionName, name, latestTimestamp);
        m_transformRepository->SetTransform(name, transform->Matrix, transform->Valid);
        result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(L"Probe", L"HMD"));
        if (result->Key)
        {
          m_debug.UpdateCoordinateSystem(L"probe", transpose(result->Value));
        }
#endif

        // We transform from quad space to image space
        // +0.5 x, -0.5 y to get square from 0-1, 0-(-1)
        // 1   0   0   0
        // 0   1   0   0
        // 0   0   1   0
        // 0.5 -0.5 0   1
        //
        // Quad space is vertex space, [-0.5,0.5]
        auto vertexToImageTransform = make_float4x4_translation(0.5f, -0.5f, 0.f);
        auto vertexToHMD = vertexToImageTransform * imageToHMDTransform;

#if defined(_DEBUG)
        m_debug.UpdateCoordinateSystem(L"vertex", vertexToHMD);
#endif

#if defined(_DEBUG)
        //m_debug.UpdateCoordinateSystem(L"imageOrigin", imageToHMDTransform);
#endif

        m_sliceEntry->SetDesiredPose(imageToHMDTransform);
      }

      m_sliceEntry->SetFrame(frame);
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process3DFrame(UWPOpenIGTLink::VideoFrame^ frame, SpatialCoordinateSystem^ hmdCoordinateSystem)
    {
      m_latestVolumeTimestamp = frame->Timestamp;

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

      IKeyValuePair<bool, float4x4>^ result = m_transformRepository->GetTransform(m_sliceToHMDName);
      if (!result->Key)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to retrieve " + m_volumeToHMDName->GetTransformName() + L" from repository.");
        return;
      }
      float4x4 volumeToHMD = transpose(result->Value);

      if (m_volumeEntry != nullptr)
      {
        m_volumeEntry->SetFrame(frame);
        m_volumeEntry->SetDesiredPose(volumeToHMD);
      }
    }
  }
}