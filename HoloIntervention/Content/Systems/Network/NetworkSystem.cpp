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
#include "Common.h"
#include "NetworkSystem.h"

// System includes
#include "NotificationSystem.h"

// Network
#include "IGTConnector.h"

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
    //----------------------------------------------------------------------------
    NetworkSystem::NetworkSystem(System::NotificationSystem& notificationSystem, Input::VoiceInput& voiceInput, StorageFolder^ configStorageFolder)
      : m_notificationSystem(notificationSystem)
      , m_voiceInput(voiceInput)
    {
      InitAsync(configStorageFolder).then([this](task<bool> initTask)
      {
        bool result;

        try
        {
          result = initTask.get();
        }
        catch (const std::exception&)
        {
          throw std::exception("Unable to initialize network system.");
        }

        m_componentReady = true;
      });

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
      Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("IGTConnector failed to find servers: ") + e.what());
      }
      });
      */
    }

    //----------------------------------------------------------------------------
    NetworkSystem::~NetworkSystem()
    {
    }

    //----------------------------------------------------------------------------
    bool NetworkSystem::IsConnected() const
    {
      bool connected(true);
      for (auto conn : m_connectors)
      {
        connected &= conn->IsConnected();
      }
      return connected;
    }

    //----------------------------------------------------------------------------
    task<bool> NetworkSystem::InitAsync(StorageFolder^ configStorageFolder)
    {
      return create_task([this, configStorageFolder]()
      {
        try
        {
          return LoadXmlDocumentAsync(L"configuration.xml", configStorageFolder).then([this](XmlDocument ^ doc)
          {
            auto xpath = ref new Platform::String(L"/HoloIntervention/IGTConnections/Connection");
            if (doc->SelectNodes(xpath)->Length == 0)
            {
              return false;
            }

            for (auto node : doc->SelectNodes(xpath))
            {
              // model, transform
              if (node->Attributes->GetNamedItem(L"Name") == nullptr)
              {
                return false;
              }
              if (node->Attributes->GetNamedItem(L"Host") == nullptr)
              {
                return false;
              }
              if (node->Attributes->GetNamedItem(L"Port") == nullptr)
              {
                return false;
              }

              Platform::String^ name = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Name")->NodeValue);
              Platform::String^ host = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Host")->NodeValue);
              Platform::String^ port = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"Port")->NodeValue);
              if (name->IsEmpty() || host->IsEmpty() || port->IsEmpty())
              {
                return false;
              }

              std::shared_ptr<Network::IGTConnector> connector = std::make_shared<Network::IGTConnector>(m_notificationSystem);
              connector->SetConnectionName(name->Data());
              connector->SetHostname(host->Data());
              try
              {
                connector->SetPort(std::stoi(port->Data()));
              }
              catch (const std::exception&) {}

              if (node->Attributes->GetNamedItem(L"ReconnectOnDrop") != nullptr)
              {
                Platform::String^ reconnectOnDrop = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"ReconnectOnDrop")->NodeValue);
                if (!reconnectOnDrop->IsEmpty())
                {
                  connector->SetReconnectOnDrop(IsEqualInsensitive(reconnectOnDrop->Data(), L"true"));
                }
              }

              if (node->Attributes->GetNamedItem(L"EmbeddedImageTransformName") != nullptr)
              {
                Platform::String^ embeddedImageTransformName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"EmbeddedImageTransformName")->NodeValue);
                if (!embeddedImageTransformName->IsEmpty())
                {
                  try
                  {
                    auto transformName = ref new UWPOpenIGTLink::TransformName(embeddedImageTransformName);
                    connector->SetEmbeddedImageTransformName(transformName);
                  }
                  catch (Platform::Exception^) {}
                }
              }

              m_connectors.push_back(connector);
            }

            return true;
          });
        }
        catch (Platform::Exception^)
        {
          return task_from_result(false);
        }
      });
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Network::IGTConnector> NetworkSystem::GetConnection(const std::wstring& name) const
    {
      auto iter = std::find_if(m_connectors.begin(), m_connectors.end(), [this, name](const std::shared_ptr<Network::IGTConnector>& connection)
      {
        return connection->GetConnectionName() == name;
      });

      if (iter != m_connectors.end())
      {
        return *iter;
      }
      return false;
    }

    //----------------------------------------------------------------------------
    void NetworkSystem::RegisterVoiceCallbacks(Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"connect"] = [this](SpeechRecognitionResult ^ result)
      {
        // Which connector?
        uint64 connectMessageId = m_notificationSystem.QueueMessage(L"Connecting...");
        m_connectors[0]->ConnectAsync(true, 4.0).then([this, connectMessageId](bool result)
        {
          m_notificationSystem.RemoveMessage(connectMessageId);
          if (result)
          {
            m_notificationSystem.QueueMessage(L"Connection successful.");
          }
          else
          {
            m_notificationSystem.QueueMessage(L"Connection failed.");
          }
        });
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
        m_connectors[0]->Disconnect();
        m_notificationSystem.QueueMessage(L"Disconnected.");
      };
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
              client->ServerHost = ref new Platform::String((prefix + L"." + ss.str()).c_str());
              client->ServerPort = 18944;

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
  }
}