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
#include "Icons.h"
#include "Log.h"
#include "NetworkSystem.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"

// IGT includes
#include <IGTCommon.h>
#include <igtlMessageBase.h>
#include <igtlStatusMessage.h>

// STL includes
#include <sstream>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace System
  {
    const float NetworkSystem::NETWORK_BLINK_TIME_SEC = 0.75;
    const double NetworkSystem::CONNECT_TIMEOUT_SEC = 3.0;
    const uint32_t NetworkSystem::RECONNECT_RETRY_DELAY_MSEC = 100;
    const uint32_t NetworkSystem::RECONNECT_RETRY_COUNT = 10;
    const uint32 NetworkSystem::DICTATION_TIMEOUT_DELAY_MSEC = 8000;
    const uint32 NetworkSystem::KEEP_ALIVE_INTERVAL_MSEC = 1000;

    //----------------------------------------------------------------------------
    task<bool> NetworkSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention");
        if (document->SelectNodes(xpath)->Length != 1)
        {
          return false;
        }

        auto rootNode = document->SelectNodes(xpath)->Item(0);

        auto connectionsElem = document->CreateElement(L"IGTConnections");

        for (auto connector : m_connectors)
        {
          auto connectionElem = document->CreateElement(L"Connection");
          connectionElem->SetAttribute(L"Name", ref new Platform::String(connector->Name.c_str()));
          if (connector->Connector->ServerHost != nullptr)
          {
            connectionElem->SetAttribute(L"Host", connector->Connector->ServerHost->DisplayName);
          }
          if (connector->Connector->ServerPort != nullptr)
          {
            connectionElem->SetAttribute(L"Port", connector->Connector->ServerPort);
          }
          if (connector->Connector->EmbeddedImageTransformName != nullptr)
          {
            connectionElem->SetAttribute(L"EmbeddedImageTransformName", connector->Connector->EmbeddedImageTransformName->GetTransformName());
          }
          connectionsElem->AppendChild(connectionElem);
        }

        rootNode->AppendChild(connectionsElem);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> NetworkSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        auto xpath = ref new Platform::String(L"/HoloIntervention/IGTConnections/Connection");
        if (document->SelectNodes(xpath)->Length == 0)
        {
          return task_from_result(false);
        }

        std::vector<task<std::shared_ptr<UI::Icon>>> modelLoadingTasks;

        for (auto node : document->SelectNodes(xpath))
        {
          if (!HasAttribute(L"Name", node))
          {
            return task_from_result(false);
          }
          if (!HasAttribute(L"Host", node))
          {
            return task_from_result(false);
          }
          if (!HasAttribute(L"Port", node))
          {
            return task_from_result(false);
          }

          Platform::String^ name = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Name")->NodeValue);
          Platform::String^ host = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Host")->NodeValue);
          Platform::String^ port = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Port")->NodeValue);
          if (name->IsEmpty() || host->IsEmpty() || port->IsEmpty())
          {
            return task_from_result(false);
          }

          std::shared_ptr<ConnectorEntry> entry = std::make_shared<ConnectorEntry>();
          entry->Name = std::wstring(name->Data());
          entry->HashedName = HashString(name->Data());
          entry->Connector = ref new UWPOpenIGTLink::IGTClient();
          entry->Connector->ServerHost = ref new HostName(host);
          entry->ErrorMessageToken = entry->Connector->ErrorMessage += ref new UWPOpenIGTLink::ErrorMessageEventHandler(std::bind(&NetworkSystem::ErrorMessageHandler, this, std::placeholders::_1, std::placeholders::_2));
          entry->ErrorMessageToken = entry->Connector->WarningMessage += ref new UWPOpenIGTLink::WarningMessageEventHandler(std::bind(&NetworkSystem::WarningMessageHandler, this, std::placeholders::_1, std::placeholders::_2));

          try
          {
            std::stoi(port->Data());
            entry->Connector->ServerPort = port;
          }
          catch (const std::exception&) {}

          if (HasAttribute(L"EmbeddedImageTransformName", node))
          {
            Platform::String^ embeddedImageTransformName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"EmbeddedImageTransformName")->NodeValue);
            if (!embeddedImageTransformName->IsEmpty())
            {
              try
              {
                auto transformName = ref new UWPOpenIGTLink::TransformName(embeddedImageTransformName);
                entry->Connector->EmbeddedImageTransformName = transformName;
              }
              catch (Platform::Exception^) {}
            }
          }

          // Create icon
          modelLoadingTasks.push_back(m_icons.AddEntryAsync(L"Assets/Models/network_icon.cmo", entry->HashedName).then([this, entry](std::shared_ptr<UI::Icon> iconEntry)
          {
            entry->Icon.m_iconEntry = iconEntry;
            return iconEntry;
          }));

          std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
          m_connectors.push_back(entry);
        }

        return when_all(begin(modelLoadingTasks), end(modelLoadingTasks)).then([this](std::vector<std::shared_ptr<UI::Icon>> entries)
        {
          m_componentReady = true;
          return true;
        });
      });
    }

    //----------------------------------------------------------------------------
    NetworkSystem::NetworkSystem(HoloInterventionCore& core, System::NotificationSystem& notificationSystem, Input::VoiceInput& voiceInput, UI::Icons& icons, Debug& debug)
      : IConfigurable(core)
      , m_notificationSystem(notificationSystem)
      , m_voiceInput(voiceInput)
      , m_icons(icons)
      , m_debug(debug)
    {
      /*
      disabled, currently crashes
      FindServersAsync().then([this](task<std::vector<std::wstring>> findServerTask)
      {
      std::vector<std::wstring> servers;
      try
      {
      servers = findServerTask.get();
      }
      catch (const std::exception& e)
      {
      LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("IGTConnector failed to find servers: ") + e.what());
      }
      });
      */
    }

    //----------------------------------------------------------------------------
    NetworkSystem::~NetworkSystem()
    {
    }

    //----------------------------------------------------------------------------
    task<bool> NetworkSystem::ConnectAsync(uint64 hashedConnectionName, double timeoutSec /*= CONNECT_TIMEOUT_SEC*/, task_options& options /*= Concurrency::task_options()*/)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });

      if (iter != end(m_connectors))
      {
        assert((*iter)->Connector != nullptr);

        (*iter)->State = CONNECTION_STATE_CONNECTING;

        return create_task((*iter)->Connector->ConnectAsync(timeoutSec), options).then([this, hashedConnectionName](task<bool> connectTask)
        {
          bool result(false);
          try
          {
            result = connectTask.get();
          }
          catch (const std::exception& e)
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("IGTConnector failed to connect: ") + e.what());
            return false;
          }

          std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
          auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
          {
            return hashedConnectionName == entry->HashedName;
          });

          if (result)
          {
            (*iter)->State = CONNECTION_STATE_CONNECTED;
          }
          else
          {
            (*iter)->State = CONNECTION_STATE_DISCONNECTED;
          }
          return result;
        });
      }
      return task_from_result(false);
    }

    //----------------------------------------------------------------------------
    task<std::vector<bool>> NetworkSystem::ConnectAsync(double timeoutSec /*= CONNECT_TIMEOUT_SEC*/, task_options& options /*= Concurrency::task_options()*/)
    {
      // Connect all connectors
      auto tasks = std::vector<task<bool>>();

      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      for (auto entry : m_connectors)
      {
        auto task = this->ConnectAsync(entry->HashedName, 4.0);
        tasks.push_back(task);
      }

      return when_all(begin(tasks), end(tasks));
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::IsConnected(uint64 hashedConnectionName) const
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      return iter == end(m_connectors) ? false : (*iter)->Connector->Connected;
    }

    //----------------------------------------------------------------------------
    HoloIntervention::System::NetworkSystem::ConnectorList NetworkSystem::GetConnectors()
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      return m_connectors;
    }

    //----------------------------------------------------------------------------
    Concurrency::task<UWPOpenIGTLink::CommandData> NetworkSystem::SendCommandAsync(uint64 hashedConnectionName, const std::wstring& commandName, const std::map<std::wstring, std::wstring>& attributes)
    {
      UWPOpenIGTLink::CommandData cmdData = {0, false};

      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });

      if (iter == end(m_connectors))
      {
        LOG_ERROR("Unable to locate connector.");
        return task_from_result(cmdData);
      }

      auto map = ref new Platform::Collections::Map<Platform::String^, Platform::String^>();
      for (auto& pair : attributes)
      {
        map->Insert(ref new Platform::String(pair.first.c_str()),
                    ref new Platform::String(pair.second.c_str()));
      }

      return create_task((*iter)->Connector->SendCommandAsync(ref new Platform::String(commandName.c_str()), map));
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::IsCommandComplete(uint64 hashedConnectionName, uint32 commandId)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });

      if (iter == end(m_connectors))
      {
        LOG_ERROR("Unable to locate connector.");
        return false;
      }

      return (*iter)->Connector->IsCommandComplete(commandId);
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::ProcessNetworkLogic(DX::StepTimer& timer)
    {
      for (auto& connector : m_connectors)
      {
        if (!connector->Icon.m_iconEntry->GetModel()->IsLoaded())
        {
          continue;
        }

        switch (connector->State)
        {
        case System::NetworkSystem::CONNECTION_STATE_CONNECTING:
        case System::NetworkSystem::CONNECTION_STATE_DISCONNECTING:
          if (connector->Icon.m_networkPreviousState != connector->State)
          {
            connector->Icon.m_networkBlinkTimer = 0.f;
          }
          else
          {
            connector->Icon.m_networkBlinkTimer += static_cast<float>(timer.GetElapsedSeconds());
            if (connector->Icon.m_networkBlinkTimer >= NETWORK_BLINK_TIME_SEC)
            {
              connector->Icon.m_networkBlinkTimer = 0.f;
              connector->Icon.m_iconEntry->GetModel()->ToggleVisible();
            }
          }
          connector->Icon.m_networkIsBlinking = true;
          break;
        case System::NetworkSystem::CONNECTION_STATE_UNKNOWN:
        case System::NetworkSystem::CONNECTION_STATE_DISCONNECTED:
        case System::NetworkSystem::CONNECTION_STATE_CONNECTION_LOST:
          connector->Icon.m_iconEntry->GetModel()->SetVisible(true);
          connector->Icon.m_networkIsBlinking = false;
          if (connector->Icon.m_wasNetworkConnected)
          {
            connector->Icon.m_iconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
            connector->Icon.m_wasNetworkConnected = false;
          }
          break;
        case System::NetworkSystem::CONNECTION_STATE_CONNECTED:
          connector->Icon.m_iconEntry->GetModel()->SetVisible(true);
          connector->Icon.m_networkIsBlinking = false;
          if (!connector->Icon.m_wasNetworkConnected)
          {
            connector->Icon.m_wasNetworkConnected = true;
            connector->Icon.m_iconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_DEFAULT);
          }
          break;
        }

        connector->Icon.m_networkPreviousState = connector->State;
      }
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"connect"] = [this](SpeechRecognitionResult ^ result)
      {
        uint64 connectMessageId = m_notificationSystem.QueueMessage(L"Connecting...");
        auto task = this->ConnectAsync(4.0);
      };

      callbackMap[L"set IP"] = [this](SpeechRecognitionResult ^ result)
      {
        // TODO : dictation
        /*
        // Which connector?
        m_dictationMatcherToken = m_voiceInput.RegisterDictationMatcher([this](const std::wstring & text)
        {
          bool matchedText(false);

          m_accumulatedDictationResult += text;
          OutputDebugStringW((m_accumulatedDictationResult + L"\n").c_str());

          if (matchedText)
          {
            m_voiceInput.RemoveDictationMatcher(m_dictationMatcherToken);
            m_voiceInput.SwitchToCommandRecognitionAsync();
            return true;
          }
          else
          {
            return false;
          }
        });
        m_voiceInput.SwitchToDictationRecognitionAsync();
        call_after([this]()
        {
          m_voiceInput.RemoveDictationMatcher(m_dictationMatcherToken);
          m_dictationMatcherToken = INVALID_TOKEN;
          m_voiceInput.SwitchToCommandRecognitionAsync();
          m_accumulatedDictationResult.clear();
        }, DICTATION_TIMEOUT_DELAY_MSEC);
        */
      };

      callbackMap[L"disconnect"] = [this](SpeechRecognitionResult ^ result)
      {
        std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
        for (auto entry : m_connectors)
        {
          entry->Connector->Disconnect();
          entry->State = CONNECTION_STATE_DISCONNECTED;
        }
        m_notificationSystem.QueueMessage(L"Disconnected.");
      };
    }


    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TransformName^ NetworkSystem::GetEmbeddedImageTransformName(uint64 hashedConnectionName) const
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      return iter == end(m_connectors) ? nullptr : (*iter)->Connector->EmbeddedImageTransformName;
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::SetEmbeddedImageTransformName(uint64 hashedConnectionName, UWPOpenIGTLink::TransformName^ name)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        (*iter)->Connector->EmbeddedImageTransformName = name;
      }
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::Disconnect(uint64 hashedConnectionName)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });

      if (iter != end(m_connectors))
      {
        (*iter)->Connector->Disconnect();
        (*iter)->State = CONNECTION_STATE_DISCONNECTED;
      }
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::GetConnectionState(uint64 hashedConnectionName, ConnectionState& state) const
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        state = (*iter)->State;
        return true;
      }
      return false;
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::SetHostname(uint64 hashedConnectionName, const std::wstring& hostname)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        (*iter)->Connector->ServerHost = ref new HostName(ref new Platform::String(hostname.c_str()));
      }
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::GetHostname(uint64 hashedConnectionName, std::wstring& hostName) const
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        hostName = (*iter)->Connector->ServerHost->DisplayName->Data();
        return true;
      }
      return false;
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::SetPort(uint64 hashedConnectionName, int32 port)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        (*iter)->Connector->ServerPort = port.ToString();
      }
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::GetPort(uint64 hashedConnectionName, int32& port) const
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        port = std::stoi((*iter)->Connector->ServerPort->Data());
        return true;
      }
      return false;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TrackedFrame^ NetworkSystem::GetTrackedFrame(uint64 hashedConnectionName, double& latestTimestamp)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        auto latestFrame = (*iter)->Connector->GetTrackedFrame(latestTimestamp);
        if (latestFrame == nullptr)
        {
          return nullptr;
        }
        try
        {
          latestTimestamp = latestFrame->Timestamp;
        }
        catch (Platform::ObjectDisposedException^) { return nullptr; }
        return latestFrame;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TransformListABI^ NetworkSystem::GetTDataFrame(uint64 hashedConnectionName, double& latestTimestamp)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        auto latestFrame = (*iter)->Connector->GetTDataFrame(latestTimestamp);
        if (latestFrame == nullptr)
        {
          return nullptr;
        }
        try
        {
          if (latestFrame->Size > 0)
          {
            latestTimestamp = latestFrame->GetAt(0)->Timestamp;
          }
          else
          {
            return nullptr;
          }
        }
        catch (Platform::ObjectDisposedException^) { return nullptr; }
        return latestFrame;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::Transform^ NetworkSystem::GetTransform(uint64 hashedConnectionName, UWPOpenIGTLink::TransformName^ transformName, double& latestTimestamp)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        auto latestFrame = (*iter)->Connector->GetTransform(transformName, latestTimestamp);
        if (latestFrame == nullptr)
        {
          return nullptr;
        }
        try
        {
          latestTimestamp = latestFrame->Timestamp;
        }
        catch (Platform::ObjectDisposedException^) { return nullptr; }
        return latestFrame;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::Polydata^ NetworkSystem::GetPolydata(uint64 hashedConnectionName, Platform::String^ name)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        auto polydata = (*iter)->Connector->GetPolydata(name);
        return polydata;
      }
      return nullptr;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::VideoFrame^ NetworkSystem::GetImage(uint64 hashedConnectionName, double& latestTimestamp)
    {
      std::lock_guard<std::recursive_mutex> guard(m_connectorsMutex);
      auto iter = std::find_if(begin(m_connectors), end(m_connectors), [hashedConnectionName](std::shared_ptr<ConnectorEntry> entry)
      {
        return hashedConnectionName == entry->HashedName;
      });
      if (iter != end(m_connectors))
      {
        auto image = (*iter)->Connector->GetImage(latestTimestamp);
        return image;
      }
      return nullptr;
    }

    //-----------------------------------------------------------------------------
    void NetworkSystem::Update(DX::StepTimer& timer)
    {
      if (!m_componentReady)
      {
        return;
      }

      ProcessNetworkLogic(timer);

      for (auto connector : m_connectors)
      {
        if (connector->State == CONNECTION_STATE_CONNECTED && !connector->Connector->Connected)
        {
          // Other end has likely dropped us, update the state (and thus the UI), and reconnect
          LOG_INFO("Connection dropped by server. Reconnecting.");
          connector->State = CONNECTION_STATE_DISCONNECTED;
          ConnectAsync(connector->HashedName, 4.0);
        }
      }
    }

    //----------------------------------------------------------------------------
    task<std::vector<std::wstring>> NetworkSystem::FindServersAsync()
    {
      return create_task([this]()
      {
        std::vector<std::wstring> results;

        auto hostNames = NetworkInformation::GetHostNames();

        for (auto host : hostNames)
        {
          if (host->Type == HostNameType::Ipv4)
          {
            std::wstring hostIP(host->ToString()->Data());
            std::wstring machineIP = hostIP.substr(hostIP.find_last_of(L'.') + 1);
            std::wstring prefix = hostIP.substr(0, hostIP.find_last_of(L'.'));

            // Given a subnet, ping all other IPs
            for (int i = 0; i < 256; ++i)
            {
              std::wstringstream ss;
              ss << i;
              if (ss.str() == machineIP)
              {
                continue;
              }

              UWPOpenIGTLink::IGTClient^ client = ref new UWPOpenIGTLink::IGTClient();
              client->ServerHost = ref new HostName(ref new Platform::String((prefix + L"." + ss.str()).c_str()));
              client->ServerPort = L"18944";

              task<bool> connectTask = create_task(client->ConnectAsync(0.5));
              bool result(false);
              try
              {
                result = connectTask.get();
              }
              catch (const std::exception&)
              {
                continue;
              }

              if (result)
              {
                client->Disconnect();
                results.push_back(prefix + L"." + ss.str());
              }
            }
          }
        }

        return results;
      });
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::ErrorMessageHandler(UWPOpenIGTLink::IGTClient^ mc, Platform::String^ msg)
    {
      for (auto& connector : m_connectors)
      {
        if (connector->Connector == mc)
        {
          WLOG_ERROR(ref new Platform::String(connector->Name.c_str()) + L" error: " + msg);
          return;
        }
      }
      WLOG_ERROR(L"Unknown connector error: " + msg);
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::WarningMessageHandler(UWPOpenIGTLink::IGTClient^ mc, Platform::String^ msg)
    {
      for (auto& connector : m_connectors)
      {
        if (connector->Connector == mc)
        {
          WLOG_WARNING(ref new Platform::String(connector->Name.c_str()) + L" warning: " + msg);
          return;
        }
      }
      WLOG_WARNING(L"Unknown connector warning: " + msg);
    }
  }
}