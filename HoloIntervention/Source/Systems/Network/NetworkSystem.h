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
#include <Input\IVoiceInput.h>
#include <Interfaces\IEngineComponent.h>
#include <Interfaces\ISerializable.h>

// IGT includes
#include <IGTCommon.h>

// OS includes
#include <ppltasks.h>

namespace igtl
{
  class TrackedFrameMessage;
}

namespace DX
{
  class StepTimer;
}

namespace Valhalla
{
  class Debug;

  namespace Input
  {
    class VoiceInput;
  }

  namespace UI
  {
    class Icon;
    class Icons;
  }

  namespace Network
  {
    class IGTConnector;
  }
}

namespace HoloIntervention
{
  namespace System
  {
    class NotificationSystem;

    class NetworkSystem : public Valhalla::IEngineComponent, public Valhalla::Input::IVoiceInput, public Valhalla::ISerializable
    {
    public:
      enum ConnectionState
      {
        CONNECTION_STATE_UNKNOWN,
        CONNECTION_STATE_CONNECTING,
        CONNECTION_STATE_CONNECTION_LOST,
        CONNECTION_STATE_DISCONNECTING,
        CONNECTION_STATE_DISCONNECTED,
        CONNECTION_STATE_CONNECTED
      };

    private:
      struct UILogicEntry
      {
        bool                                        m_wasNetworkConnected = true;
        bool                                        m_networkIsBlinking = true;
        ConnectionState                             m_networkPreviousState = CONNECTION_STATE_UNKNOWN;
        float                                       m_networkBlinkTimer = 0.f;
        std::shared_ptr<Valhalla::UI::Icon>         m_iconEntry = nullptr;
      };

      struct ConnectorEntry
      {
        std::wstring                                Name = L""; // For saving back to disk
        uint64                                      HashedName = 0;
        ConnectionState                             State = CONNECTION_STATE_UNKNOWN;
        UWPOpenIGTLink::IGTClient^                  Connector = nullptr;
        UILogicEntry                                Icon;
        Windows::Foundation::EventRegistrationToken ErrorMessageToken;
        Windows::Foundation::EventRegistrationToken WarningMessageToken;
      };
      typedef std::vector<std::shared_ptr<ConnectorEntry>> ConnectorList;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      NetworkSystem(Valhalla::ValhallaCore& core, System::NotificationSystem& notificationSystem, Valhalla::Input::VoiceInput& voiceInput, Valhalla::UI::Icons& icons, Valhalla::Debug& debug);
      virtual ~NetworkSystem();

      /// IVoiceInput functions
      void RegisterVoiceCallbacks(Valhalla::Input::VoiceInputCallbackMap& callbackMap);

      /// Connect all known connectors
      Concurrency::task<std::vector<bool>> ConnectAsync(double timeoutSec = CONNECT_TIMEOUT_SEC, Concurrency::task_options& options = Concurrency::task_options());

      /// Connect a specific connector
      Concurrency::task<bool> ConnectAsync(uint64 hashedConnectionName, double timeoutSec = CONNECT_TIMEOUT_SEC, Concurrency::task_options& options = Concurrency::task_options());

      bool IsConnected(uint64 hashedConnectionName) const;

      ConnectorList GetConnectors();

      Concurrency::task<UWPOpenIGTLink::CommandData> SendCommandAsync(uint64 hashedConnectionName, const std::wstring& commandName, const std::map<std::wstring, std::wstring>& attributes);

      bool IsCommandComplete(uint64 hashedConnectionName, uint32 commandId);

      UWPOpenIGTLink::TransformName^ GetEmbeddedImageTransformName(uint64 hashedConnectionName) const;
      void SetEmbeddedImageTransformName(uint64 hashedConnectionName, UWPOpenIGTLink::TransformName^ name);

      void Disconnect(uint64 hashedConnectionName);
      bool GetConnectionState(uint64 hashedConnectionName, ConnectionState& state) const;

      void SetHostname(uint64 hashedConnectionName, const std::wstring& hostname);
      bool GetHostname(uint64 hashedConnectionName, std::wstring& hostName) const;

      void SetPort(uint64 hashedConnectionName, int32 port);
      bool GetPort(uint64 hashedConnectionName, int32& port) const;

      UWPOpenIGTLink::TrackedFrame^ GetTrackedFrame(uint64 hashedConnectionName, double& latestTimestamp);
      UWPOpenIGTLink::TransformListABI^ GetTDataFrame(uint64 hashedConnectionName, double& latestTimestamp);
      UWPOpenIGTLink::Transform^ GetTransform(uint64 hashedConnectionName, UWPOpenIGTLink::TransformName^ transformName, double& latestTimestamp);
      UWPOpenIGTLink::Polydata^ GetPolydata(uint64 hashedConnectionName, Platform::String^ name);
      UWPOpenIGTLink::VideoFrame^ GetImage(uint64 hashedConnectionName, double& latestTimestamp);

      void Update(DX::StepTimer& timer);

    protected:
      void ProcessNetworkLogic(DX::StepTimer& timer);
      Concurrency::task<std::vector<std::wstring>> FindServersAsync();

    protected:
      void ErrorMessageHandler(UWPOpenIGTLink::IGTClient^ mc, Platform::String^ msg);
      void WarningMessageHandler(UWPOpenIGTLink::IGTClient^ mc, Platform::String^ msg);

    protected:
      // Cached entries
      System::NotificationSystem&                   m_notificationSystem;
      Valhalla::Input::VoiceInput&                  m_voiceInput;
      Valhalla::UI::Icons&                          m_icons;
      Valhalla::Debug&                              m_debug;

      std::wstring                                  m_accumulatedDictationResult;
      uint64                                        m_dictationMatcherToken;

      // Icons that this subsystem manages
      static const float                            NETWORK_BLINK_TIME_SEC;

      mutable std::recursive_mutex                  m_connectorsMutex;
      ConnectorList                                 m_connectors;

      // Constants relating to IGT behavior
      static const double                           CONNECT_TIMEOUT_SEC;
      static const uint32                           RECONNECT_RETRY_DELAY_MSEC;
      static const uint32                           RECONNECT_RETRY_COUNT;
      static const uint32                           DICTATION_TIMEOUT_DELAY_MSEC;
      static const uint32                           KEEP_ALIVE_INTERVAL_MSEC;
    };
  }
}