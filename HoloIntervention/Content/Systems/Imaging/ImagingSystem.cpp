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

#include "pch.h"

// Local includes
#include "AppView.h"
#include "ImagingSystem.h"

// Common includes
#include "StepTimer.h"

// Network includes
#include "IGTLinkIF.h"

// System includes
#include "NotificationSystem.h"

// Rendering includes
#include "SliceRenderer.h"

using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Media::SpeechRecognition;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    ImagingSystem::ImagingSystem()
    {
      m_componentReady = true;
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
        return HoloIntervention::instance()->GetSliceRenderer().GetSlicePose(m_sliceToken);
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
        return HoloIntervention::instance()->GetSliceRenderer().GetSliceVelocity(m_sliceToken);
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
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Slice showing.");
          HoloIntervention::instance()->GetSliceRenderer().ShowSlice(m_sliceToken);
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No slice available.");
      };

      callbackMap[L"slice off"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HasSlice())
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Slice hidden.");
          HoloIntervention::instance()->GetSliceRenderer().HideSlice(m_sliceToken);
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No slice available.");
      };

      callbackMap[L"lock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!HasSlice())
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No slice to head-lock!");
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Slice is now head-locked.");
        HoloIntervention::instance()->GetSliceRenderer().SetSliceHeadlocked(m_sliceToken, true);
      };

      callbackMap[L"unlock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (!HasSlice())
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No slice to unlock!");
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Slice is now in world-space.");
        HoloIntervention::instance()->GetSliceRenderer().SetSliceHeadlocked(m_sliceToken, false);
      };

      callbackMap[L"piecewise linear transfer function"] = [this](SpeechRecognitionResult ^ result)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Using built-in piecewise linear transfer function.");
        // TODO : how to define which volume to apply to?
      };
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ coordSystem)
    {
      if (!HasSlice())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_sliceToken = HoloIntervention::instance()->GetSliceRenderer().AddSlice(Network::IGTLinkIF::GetSharedImagePtr(frame),
                       frame->FrameSize->GetAt(0),
                       frame->FrameSize->GetAt(1),
                       (DXGI_FORMAT)frame->GetPixelFormat(true),
                       transpose(frame->EmbeddedImageTransform),
                       coordSystem);
      }
      else
      {
        HoloIntervention::instance()->GetSliceRenderer().UpdateSlice(m_sliceToken,
            Network::IGTLinkIF::GetSharedImagePtr(frame),
            frame->Width,
            frame->Height,
            (DXGI_FORMAT)frame->GetPixelFormat(true),
            transpose(frame->EmbeddedImageTransform),
            coordSystem);
      }
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame, SpatialCoordinateSystem^ coordSystem)
    {
      if (!HasSlice())
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_volumeToken = HoloIntervention::instance()->GetVolumeRenderer().AddVolume(Network::IGTLinkIF::GetSharedImagePtr(frame),
                        frame->FrameSize->GetAt(0),
                        frame->FrameSize->GetAt(1),
                        (DXGI_FORMAT)frame->GetPixelFormat(true),
                        transpose(frame->EmbeddedImageTransform),
                        coordSystem);
      }
      else
      {
        HoloIntervention::instance()->GetVolumeRenderer().AddVolume(m_volumeToken,
            Network::IGTLinkIF::GetSharedImagePtr(frame),
            frame->Width,
            frame->Height,
            (DXGI_FORMAT)frame->GetPixelFormat(true),
            transpose(frame->EmbeddedImageTransform),
            coordSystem);
      }
    }
  }
}