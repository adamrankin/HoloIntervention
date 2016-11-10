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
#include "VolumeRenderer.h"

using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    ImagingSystem::ImagingSystem()
    {
    }

    //----------------------------------------------------------------------------
    ImagingSystem::~ImagingSystem()
    {
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Update(UWPOpenIGTLink::TrackedFrame^ frame, const DX::StepTimer& timer)
    {
      if (frame->HasImage())
      {
        if (frame->FrameSize->GetAt(2) == 1)
        {
          Process2DFrame(frame);
        }
        else if (frame->FrameSize->GetAt(2) > 1)
        {
          Process3DFrame(frame);
        }
      }
    }

    //----------------------------------------------------------------------------
    bool ImagingSystem::HasSlice() const
    {
      return m_sliceToken != Rendering::SliceRenderer::INVALID_SLICE_INDEX;
    }

    //----------------------------------------------------------------------------
    float4x4 ImagingSystem::GetSlicePose() const
    {
      float4x4 mat(float4x4::identity());
      HoloIntervention::instance()->GetSliceRenderer().GetSlicePose(m_sliceToken, mat);
      return mat;
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"lock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_sliceToken == Rendering::SliceRenderer::INVALID_SLICE_INDEX)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No slice to head-lock!");
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Slice is now head-locked.");
        HoloIntervention::instance()->GetSliceRenderer().SetSliceHeadlocked(m_sliceToken, true);
      };

      callbackMap[L"unlock slice"] = [this](SpeechRecognitionResult ^ result)
      {
        if (m_sliceToken == Rendering::SliceRenderer::INVALID_SLICE_INDEX)
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
        HoloIntervention::instance()->GetVolumeRenderer().SetTransferFunctionTypeAsync(Rendering::VolumeRenderer::TransferFunction_Piecewise_Linear);
      };
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process2DFrame(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      if (m_sliceToken == Rendering::SliceRenderer::INVALID_SLICE_INDEX)
      {
        // For now, our slice renderer only draws one slice, in the future, it should be able to draw more
        m_sliceToken = HoloIntervention::instance()->GetSliceRenderer().AddSlice(Network::IGTLinkIF::GetSharedImagePtr(frame),
                       frame->FrameSize->GetAt(0),
                       frame->FrameSize->GetAt(1),
                       (DXGI_FORMAT)frame->PixelFormat,
                       frame->EmbeddedImageTransform);
      }
      else
      {
        HoloIntervention::instance()->GetSliceRenderer().UpdateSlice(m_sliceToken,
            Network::IGTLinkIF::GetSharedImagePtr(frame),
            frame->Width,
            frame->Height,
            (DXGI_FORMAT)frame->PixelFormat,
            frame->EmbeddedImageTransform);
      }
    }

    //----------------------------------------------------------------------------
    void ImagingSystem::Process3DFrame(UWPOpenIGTLink::TrackedFrame^ frame)
    {
      OutputDebugStringA("Process3DFrame : The method or operation is not implemented.");
    }
  }
}