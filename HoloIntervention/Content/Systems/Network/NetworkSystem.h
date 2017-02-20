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
#include "IEngineComponent.h"
#include "IVoiceInput.h"

// Network includes
#include "IGTConnector.h"

// OS includes
#include <ppltasks.h>

namespace HoloIntervention
{
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
    class NotificationSystem;

    class NetworkSystem : public IEngineComponent, public Sound::IVoiceInput
    {
    public:
      typedef std::vector<std::shared_ptr<Network::IGTConnector>> ConnectorList;

    public:
      NetworkSystem(System::NotificationSystem& notificationSystem, Input::VoiceInput& voiceInput);
      virtual ~NetworkSystem();

      bool IsConnected() const;
      Concurrency::task<bool> InitAsync();
      std::shared_ptr<Network::IGTConnector> GetConnection(const std::wstring& name) const;

      /// IVoiceInput functions
      void RegisterVoiceCallbacks(Sound::VoiceInputCallbackMap& callbackMap);

    protected:
      Concurrency::task<std::vector<std::wstring>> FindServersAsync();

    protected:
      System::NotificationSystem&   m_notificationSystem;
      Input::VoiceInput&            m_voiceInput;
      ConnectorList                 m_connectors;
    };
  }
}