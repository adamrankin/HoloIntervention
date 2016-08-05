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
#include <vccorlib.h>

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
      // TODO : remove temp code
      m_igtClient->ServerHost = L"172.16.80.1";
      //m_igtClient->RegisterTrackedFrameCallback(&TrackedFrameCallback);
    }

    //----------------------------------------------------------------------------
    IGTLinkIF::~IGTLinkIF()
    {
    }

    //----------------------------------------------------------------------------
    concurrency::task<bool> IGTLinkIF::ConnectAsync(double timeoutSec)
    {
      return create_task(m_igtClient->ConnectAsync(timeoutSec));
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::Disconnect()
    {
      m_igtClient->Disconnect();
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::SetHostname(const std::wstring& hostname)
    {
      m_igtClient->ServerHost = ref new Platform::String(hostname.c_str());
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::SetPort(int32 port)
    {
      m_igtClient->ServerPort = port;
    }

    //----------------------------------------------------------------------------
    void IGTLinkIF::TrackedFrameCallback(igtl::TrackedFrameMessage* message)
    {
      // TODO : this function will be called whenever a tracked frame is received
    }
  }
}