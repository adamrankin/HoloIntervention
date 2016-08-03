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
#include "IGTLinkIF.h"
#include "NotificationsAPI.h"

// Windows includes
#include <ppltasks.h>

using namespace Concurrency;

namespace TrackedUltrasound
{
  namespace IGTLink
  {
    const double IGTLinkIF::CONNECT_TIMEOUT_SEC = 3.0;

    //----------------------------------------------------------------------------
    IGTLinkIF::IGTLinkIF()
      : m_igtClient(ref new UWPOpenIGTLink::IGTLinkClient())
    {
    }

    //----------------------------------------------------------------------------
    IGTLinkIF::~IGTLinkIF()
    {
    }

    //----------------------------------------------------------------------------
    bool IGTLinkIF::Connect()
    {
      auto connectTask = create_task(m_igtClient->ConnectAsync(CONNECT_TIMEOUT_SEC)).then([this](bool result) -> bool
      {
        if (!result)
        {
          TrackedUltrasound::instance()->GetNotificationAPI().QueueMessage(L"IGT Connection failed.");
        }

        return result;
      });

      return connectTask.get();
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::Disconnect()
    {
      m_igtClient->DisconnectAsync();
    }
  }
}