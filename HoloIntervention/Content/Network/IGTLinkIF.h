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
#include "IVoiceInput.h"

// Windows includes
#include <ppltasks.h>

// IGT forward declarations
namespace igtl
{
  class TrackedFrameMessage;
}

namespace HoloIntervention
{
  namespace Network
  {
    class IGTLinkIF : public Sound::IVoiceInput
    {
    public:
      IGTLinkIF();
      ~IGTLinkIF();

      /// Connect to the server specified by SetHostname() and SetPort()
      /// If connected to a server, disconnects first.
      concurrency::task<bool> ConnectAsync( double timeoutSec = CONNECT_TIMEOUT_SEC );

      /// Disconnect from the server
      void Disconnect();

      /// Accessor to connected state
      bool IsConnected();

      /// Set the hostname to connect to
      void SetHostname( const std::wstring& hostname );

      /// Get the hostname to connect to
      std::wstring GetHostname() const;

      /// Set the port to connect to
      void SetPort( int32 port );

      /// Retrieve the latest tracked frame
      bool GetLatestTrackedFrame( UWPOpenIGTLink::TrackedFrame^ frame, double* latestTimestamp );

      /// Retrieve the latest command
      bool GetLatestCommand( UWPOpenIGTLink::Command^ cmd, double* latestTimestamp );

      /// IVoiceInput functions
      void RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg );

    public:
      // Static helper functions
      static std::shared_ptr<byte> GetSharedImagePtr( UWPOpenIGTLink::TrackedFrame^ frame );

    protected:
      // Link to an IGT server
      UWPOpenIGTLink::IGTLinkClient^    m_igtClient;

      // Constants relating to IGT behavior
      static const double               CONNECT_TIMEOUT_SEC;
    };
  }
}