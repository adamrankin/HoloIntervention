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
#include "SpatialInputHandler.h"

// STL includes
#include <functional>

using namespace Windows::Foundation;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;

namespace HoloIntervention
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    SpatialInputHandler::SpatialInputHandler()
    {
      m_interactionManager = SpatialInteractionManager::GetForCurrentView();
      m_sourcePressedEventToken =
        m_interactionManager->SourcePressed += ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(bind(&SpatialInputHandler::OnSourcePressed, this, _1, _2));
    }

    //----------------------------------------------------------------------------
    SpatialInputHandler::~SpatialInputHandler()
    {
      m_interactionManager->SourcePressed -= m_sourcePressedEventToken;
      m_interactionManager->SourceDetected -= m_sourceDetectedEventToken;
    }

    //----------------------------------------------------------------------------
    SpatialInteractionSourceState^ SpatialInputHandler::CheckForPressedInput()
    {
      SpatialInteractionSourceState^ sourceState = m_sourceState;
      m_sourceState = nullptr;
      return sourceState;
    }

    //----------------------------------------------------------------------------
    void SpatialInputHandler::OnSourcePressed(SpatialInteractionManager^ sender, SpatialInteractionSourceEventArgs^ args)
    {
      m_sourceState = args->State;
    }
  }
}