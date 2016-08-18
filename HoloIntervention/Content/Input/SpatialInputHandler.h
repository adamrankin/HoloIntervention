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

namespace HoloIntervention
{
  namespace Input
  {
    class SpatialInputHandler
    {
    public:
      SpatialInputHandler();
      ~SpatialInputHandler();

      Windows::UI::Input::Spatial::SpatialInteractionSourceState^ CheckForPressedInput();

    private:
      // Interaction event handler.
      void OnSourcePressed( Windows::UI::Input::Spatial::SpatialInteractionManager^ sender,
                            Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args );

      // API objects used to process gesture input, and generate gesture events.
      Windows::UI::Input::Spatial::SpatialInteractionManager^ m_interactionManager;

      // Event registration token.
      Windows::Foundation::EventRegistrationToken m_sourcePressedEventToken;
      Windows::Foundation::EventRegistrationToken m_sourceDetectedEventToken;

      // Used to indicate that a Pressed input event was received this frame.
      Windows::UI::Input::Spatial::SpatialInteractionSourceState^ m_sourceState = nullptr;
    };
  }
}