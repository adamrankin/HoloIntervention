/*====================================================================
Copyright(c) 2017 Adam Rankin


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
    class SpatialSourceHandler
    {
    public:
      SpatialSourceHandler(Windows::UI::Input::Spatial::SpatialInteractionSource^ source);
      ~SpatialSourceHandler();

      void OnSourceUpdated(Windows::UI::Input::Spatial::SpatialInteractionSourceState^ state, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
      void OnInteractionDetected(Windows::UI::Input::Spatial::SpatialInteraction^ interaction, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);
      void OnSourcePressed(Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args);

    protected:
      // For Hands, we map commands based on Gestures detection
      void OnTapped(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialTappedEventArgs^ args);
      void OnNavigationStarted(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialNavigationStartedEventArgs^ args);
      void OnNavigationCompleted(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialNavigationCompletedEventArgs^ args);
      void OnManipulationStarted(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialManipulationStartedEventArgs^ args);
      void OnManipulationCanceled(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialManipulationCanceledEventArgs^ args);
      void OnManipulationCompleted(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialManipulationCompletedEventArgs^ args);
      void OnManipulationUpdated(Windows::UI::Input::Spatial::SpatialGestureRecognizer^ sender, Windows::UI::Input::Spatial::SpatialManipulationUpdatedEventArgs^ args);

      bool DetectIntersection(Windows::UI::Input::Spatial::SpatialPointerPose^ pointerPose);

    protected:
      unsigned int                                              m_sourceId;
      Windows::UI::Input::Spatial::SpatialInteractionSourceKind m_sourceKind;

      Windows::UI::Input::Spatial::SpatialGestureRecognizer^    m_gestureRecognizer;
      Windows::Perception::Spatial::SpatialCoordinateSystem^    m_coordinateSystem;
      Windows::Foundation::Numerics::float3                     m_initialPosition;

      Windows::Foundation::EventRegistrationToken               m_tappedToken;
      Windows::Foundation::EventRegistrationToken               m_navigationStartedToken;
      Windows::Foundation::EventRegistrationToken               m_navigationCompletedToken;
      Windows::Foundation::EventRegistrationToken               m_manipulationStartedToken;
      Windows::Foundation::EventRegistrationToken               m_manipulationCompletedToken;
      Windows::Foundation::EventRegistrationToken               m_manipulationCanceledToken;
      Windows::Foundation::EventRegistrationToken               m_manipulationUpdatedToken;
    };
  }
}