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
#include "NotificationRenderer.h"

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    NotificationRenderer::NotificationRenderer()
    {
    }


    //----------------------------------------------------------------------------
    NotificationRenderer::~NotificationRenderer()
    {
    }

    const double NotificationRenderer::MAXIMUM_REQUESTED_DURATION_SEC = 10.0;

    const double NotificationRenderer::DEFAULT_NOTIFICATION_DURATION_SEC = 3.0;

    //----------------------------------------------------------------------------
    void NotificationRenderer::QueueMessage( const std::string& message, double duration )
    {
      this->QueueMessage( std::wstring( message.begin(), message.end() ), duration );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::QueueMessage( Platform::String^ message, double duration )
    {
      this->QueueMessage( std::wstring( message->Data() ), duration );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::QueueMessage( const std::wstring& message, double duration )
    {
      uint64 ticks( 0 );
      try { uint64 ticks = DX::StepTimer::GetTicks(); }
      catch ( Platform::FailureException^ ex ) { throw std::exception( "Unable to queue message. Cannot grab timestamp." ); return; }

      duration = clamp<double>( duration, 0.1, MAXIMUM_REQUESTED_DURATION_SEC );

      MessageTimeDuration mt( message, ticks, duration );
      m_messages.push_back( mt );
    }

    //----------------------------------------------------------------------------
    void NotificationRenderer::Update()
    {
      uint64 ticks( 0 );
      try { uint64 ticks = DX::StepTimer::GetTicks(); }
      catch ( Platform::FailureException^ ex ) {return;}

      if ( !m_notificationActive && m_messages.size() > 0 )
      {
        // Show the new message!
      }
      else if ( m_notificationActive && m_messages.size() > 0 )
      {
        // Check to see if the current message's time is up
        auto current = m_messages.front();
        uint64 diff = ticks - std::get<1>( current );
        double diffSec = DX::StepTimer::TicksToSeconds( diff );

        if ( diffSec > std::get<2>( current ) )
        {
          // The time for the current message has ended, clear it and move on!
          m_messages.pop_front();

          if ( m_messages.size() > 0 )
          {
            // TODO : show next message
          }
          else
          {
            // TODO : hide message
            m_notificationActive = false;
          }
        }
      }
    }
  }
}