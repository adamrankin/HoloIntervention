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

// Network includes
#include "IGTConnector.h"

// Rendering includes
#include "ModelEntry.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
  }

  namespace Input
  {
    class VoiceInput;
  }

  namespace Network
  {
    class IGTConnector;
  }

  namespace System
  {
    class IconEntry;
    class NotificationSystem;
    class RegistrationSystem;

    typedef std::vector<std::shared_ptr<IconEntry>> IconEntryList;

    class IconSystem : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      IconSystem(NotificationSystem& notificationSystem, RegistrationSystem& registrationSystem, Network::IGTConnector& igtConnector, Input::VoiceInput& voiceInput, Rendering::ModelRenderer& modelRenderer);
      ~IconSystem();

      void Update(DX::StepTimer& timer, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      std::shared_ptr<IconEntry> AddEntry(const std::wstring& modelName);
      bool RemoveEntry(uint64 entryId);
      std::shared_ptr<IconEntry> GetEntry(uint64 entryId);

    protected:
      void ProcessNetworkLogic(DX::StepTimer&);
      void ProcessCameraLogic(DX::StepTimer&);
      void ProcessMicrophoneLogic(DX::StepTimer&);

    protected:
      uint64                          m_nextValidEntry = 0;
      IconEntryList                   m_iconEntries;

      // Cached entries to model renderer
      Rendering::ModelRenderer&       m_modelRenderer;
      NotificationSystem&             m_notificationSystem;
      RegistrationSystem&             m_registrationSystem;
      Network::IGTConnector&          m_IGTConnector;
      Input::VoiceInput&              m_voiceInput;

      // Icons that this subsystem manages
      std::shared_ptr<IconEntry>      m_networkIcon = nullptr;
      std::shared_ptr<IconEntry>      m_cameraIcon = nullptr;
      std::shared_ptr<IconEntry>      m_microphoneIcon = nullptr;

      // Network logic variables
      bool                            m_wasNetworkConnected = true;
      bool                            m_networkIsBlinking = true;
      Network::ConnectionState        m_networkPreviousState = Network::CONNECTION_STATE_UNKNOWN;
      float                           m_networkBlinkTimer = 0.f;
      static const float              NETWORK_BLINK_TIME_SEC;

      // Camera logic variables
      float                           m_cameraBlinkTimer = 0.f;
      bool                            m_wasCameraOn = true;
      static const float              CAMERA_BLINK_TIME_SEC;

      // Microphone logic variables
      float                           m_microphoneBlinkTimer = 0.f;
      bool                            m_wasHearingSound = true;
      static const float              MICROPHONE_BLINK_TIME_SEC;

      // Shared variables
      static const float              ANGLE_BETWEEN_ICONS_RAD;
      static const float              ICON_START_ANGLE;
      static const float              ICON_UP_ANGLE;
      static const float              ICON_SIZE_METER;
    };
  }
}