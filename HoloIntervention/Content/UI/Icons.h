/*====================================================================
Copyright(c) 2017 Adam Rankin


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
#include "IconEntry.h"

// Rendering includes
#include "ModelEntry.h"

// System includes
#include "NetworkSystem.h"

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

  namespace Network
  {
    class IGTConnector;
  }

  namespace System
  {
    class NotificationSystem;
    class ToolSystem;
  }

  namespace UI
  {
    typedef std::vector<std::shared_ptr<IconEntry>> IconEntryList;

    class Icons : public IConfigurable, public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      Icons(System::NotificationSystem& notificationSystem, System::NetworkSystem& networkSystem, Rendering::ModelRenderer& modelRenderer);
      ~Icons();

      void Update(DX::StepTimer& timer, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      Concurrency::task<std::shared_ptr<IconEntry>> AddEntryAsync(const std::wstring& modelName, std::wstring userValue = L"");
      Concurrency::task<std::shared_ptr<IconEntry>> AddEntryAsync(const std::wstring& modelName, uint64 userValue = 0);
      Concurrency::task<std::shared_ptr<IconEntry>> AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, std::wstring userValue = L"");
      Concurrency::task<std::shared_ptr<IconEntry>> AddEntryAsync(std::shared_ptr<Rendering::ModelEntry> modelEntry, uint64 userValue = 0);
      bool RemoveEntry(uint64 entryId);
      std::shared_ptr<IconEntry> GetEntry(uint64 entryId);

    protected:
      void ProcessNetworkLogic(DX::StepTimer&);
      void ProcessMicrophoneLogic(DX::StepTimer&);

    protected:
      std::mutex                                m_entryMutex;
      uint64                                    m_nextValidEntry = 0;
      IconEntryList                             m_iconEntries;
      std::atomic_bool                          m_iconsShowing = true;

      // Cached entries to model renderer
      Rendering::ModelRenderer&                 m_modelRenderer;
      System::NotificationSystem&               m_notificationSystem;
      System::NetworkSystem&                    m_networkSystem;
      Input::VoiceInput&                        m_voiceInput;

      // Icons that this subsystem manages
      std::vector<std::shared_ptr<IconEntry>>   m_networkIcons;
      std::shared_ptr<IconEntry>                m_microphoneIcon = nullptr;

      // Network logic variables
      struct NetworkLogicEntry
      {
        bool                                    m_wasNetworkConnected = true;
        bool                                    m_networkIsBlinking = true;
        System::NetworkSystem::ConnectionState  m_networkPreviousState = System::NetworkSystem::CONNECTION_STATE_UNKNOWN;
        float                                   m_networkBlinkTimer = 0.f;
      };
      std::vector<NetworkLogicEntry>            m_networkLogicEntries;
      static const float                        NETWORK_BLINK_TIME_SEC;

      // Microphone logic variables
      float                                     m_microphoneBlinkTimer = 0.f;
      bool                                      m_wasHearingSound = true;
      static const float                        MICROPHONE_BLINK_TIME_SEC;

      // Shared variables
      static const float                        ANGLE_BETWEEN_ICONS_RAD;
      static const float                        ICON_START_ANGLE;
      static const float                        ICON_UP_ANGLE;
      static const float                        ICON_SIZE_METER;
    };
  }
}