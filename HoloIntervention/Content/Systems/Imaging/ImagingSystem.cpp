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
#include "NotificationSystem.h"

// Rendering includes
#include "VolumeRenderer.h"
#include "SliceRenderer.h"

// Unnecessary, but removes intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Data::Xml::Dom;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    ImagingSystem::ImagingSystem(RegistrationSystem& registrationSystem, NotificationSystem& notificationSystem, Rendering::SliceRenderer& sliceRenderer, Rendering::VolumeRenderer& volumeRenderer)
      : m_notificationSystem(notificationSystem)
      , m_registrationSystem(registrationSystem)
      , m_sliceRenderer(sliceRenderer)
      , m_volumeRenderer(volumeRenderer)
    {
      try
      {
        InitializeTransformRepositoryAsync(m_transformRepository, L"Assets\\Data\\configuration.xml").then([this]()
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
        GetXmlDocumentFromFileAsync(L"Assets\\Data\\configuration.xml").then([this](XmlDocument ^ doc)
        {
          auto xpath = ref new Platform::String(L"/HoloIntervention/VolumeRendering");
          if (doc->SelectNodes(xpath)->Length != 1)
          {
            // No configuration found, use defaults
            return;
          }

          IXmlNode^ volRendering = doc->SelectNodes(xpath)->Item(0);
          Platform::String^ fromAttribute = dynamic_cast<Platform::String^>(volRendering->Attributes->GetNamedItem(L"From")->NodeValue);
          Platform::String^ toAttribute = dynamic_cast<Platform::String^>(volRendering->Attributes->GetNamedItem(L"To")->NodeValue);
          if (fromAttribute->IsEmpty() || toAttribute->IsEmpty())
          {
            return;
          }
          else
          {
            m_fromCoordFrame = std::wstring(fromAttribute->Data());
            m_toCoordFrame = std::wstring(toAttribute->Data());
            m_imageToHMDName = ref new UWPOpenIGTLink::TransformName(fromAttribute, toAttribute);
          }
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
    void ImagingSystem::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer, SpatialCoordinateSystem^ coordSystem)
    {
      if (frame->HasImage())
      {
        if (frame->FrameSize->GetAt(2) == 1)
        {
          Process2DFrame(frame, coordSystem);
        }
        else if (frame->FrameSize->GetAt(2) > 1)
        {
          Process3DFrame(frame, coordSystem);
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
    void ImagingSystem::Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ coordSystem)
    {
      // TODO : apply registration to incoming transform
      if (!HasSlice())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_sliceToken = m_sliceRenderer.AddSlice(Network::IGTConnector::GetSharedImagePtr(frame),
                                                frame->FrameSize->GetAt(0),
                                                frame->FrameSize->GetAt(1),
                                                (DXGI_FORMAT)frame->GetPixelFormat(true),
                                                transpose(frame->EmbeddedImageTransform));
      }
      else
      {
        m_sliceRenderer.UpdateSlice(m_sliceToken,
                                    Network::IGTConnector::GetSharedImagePtr(frame),
                                    frame->Width,
                                    frame->Height,
                                    (DXGI_FORMAT)frame->GetPixelFormat(true),
                                    transpose(frame->EmbeddedImageTransform));
      }
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ coordSystem)
    {
      // TODO : apply registration to incoming transform
      if (!HasVolume())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_volumeToken = m_volumeRenderer.AddVolume(Network::IGTConnector::GetSharedImagePtr(frame),
                        frame->Width,
                        frame->Height,
                        frame->Depth,
                        (DXGI_FORMAT)frame->GetPixelFormat(true),
                        transpose(frame->EmbeddedImageTransform));
      }
      else
      {
        m_volumeRenderer.UpdateVolume(m_volumeToken,
                                      Network::IGTConnector::GetSharedImagePtr(frame),
                                      frame->Width,
                                      frame->Height,
                                      frame->Depth,
                                      (DXGI_FORMAT)frame->GetPixelFormat(true),
                                      transpose(frame->EmbeddedImageTransform));
      }
    }
  }
}