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

// Local includes
#include "pch.h"
#include "SpatialSourceHandler.h"

using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

static const double NAVIGATION_THRESHOLD = .3;

namespace HoloIntervention
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    SpatialSourceHandler::SpatialSourceHandler(SpatialInteractionSource^ source)
      : m_sourceKind(source->Kind)
      , m_sourceId(source->Id)
      , m_gestureRecognizer(ref new SpatialGestureRecognizer(SpatialGestureSettings::Tap | SpatialGestureSettings::ManipulationTranslate))
    {
      // Handle Tapped gesture
      m_tappedToken =
        m_gestureRecognizer->Tapped +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialTappedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnTapped, this, std::placeholders::_1, std::placeholders::_2)
          );

      // Handle Navigation gesture
      m_navigationStartedToken =
        m_gestureRecognizer->NavigationStarted +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialNavigationStartedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnNavigationStarted, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_navigationCompletedToken =
        m_gestureRecognizer->NavigationCompleted +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialNavigationCompletedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnNavigationCompleted, this, std::placeholders::_1, std::placeholders::_2)
          );

      // Handle Manipulation gesture
      m_manipulationStartedToken =
        m_gestureRecognizer->ManipulationStarted +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialManipulationStartedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnManipulationStarted, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_manipulationCanceledToken =
        m_gestureRecognizer->ManipulationCanceled +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialManipulationCanceledEventArgs^>(
            std::bind(&SpatialSourceHandler::OnManipulationCanceled, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_manipulationCompletedToken =
        m_gestureRecognizer->ManipulationCompleted +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialManipulationCompletedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnManipulationCompleted, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_manipulationUpdatedToken =
        m_gestureRecognizer->ManipulationUpdated +=
          ref new TypedEventHandler<SpatialGestureRecognizer^, SpatialManipulationUpdatedEventArgs^>(
            std::bind(&SpatialSourceHandler::OnManipulationUpdated, this, std::placeholders::_1, std::placeholders::_2)
          );
    }

    //----------------------------------------------------------------------------
    SpatialSourceHandler::~SpatialSourceHandler()
    {
      m_gestureRecognizer->Tapped -= m_tappedToken;
      m_gestureRecognizer->NavigationStarted -= m_navigationStartedToken;
      m_gestureRecognizer->NavigationCompleted -= m_navigationCompletedToken;
      m_gestureRecognizer->ManipulationStarted -= m_manipulationStartedToken;
      m_gestureRecognizer->ManipulationCanceled -= m_manipulationCanceledToken;
      m_gestureRecognizer->ManipulationCompleted -= m_manipulationCompletedToken;
      m_gestureRecognizer->ManipulationUpdated -= m_manipulationUpdatedToken;
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnInteractionDetected(SpatialInteraction^ interaction, SpatialCoordinateSystem^ coordinateSystem)
    {
      try
      {
        m_gestureRecognizer->TrySetGestureSettings(SpatialGestureSettings::Tap | SpatialGestureSettings::ManipulationTranslate);
      }
      catch (...)
      {
        m_coordinateSystem = nullptr;
        return;
      }

      m_coordinateSystem = coordinateSystem;
      m_gestureRecognizer->CaptureInteraction(interaction);
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnSourcePressed(Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args)
    {

    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnNavigationStarted(SpatialGestureRecognizer^ sender, SpatialNavigationStartedEventArgs^ args)
    {
      if (DetectIntersection(args->TryGetPointerPose(m_coordinateSystem)))
      {

      }
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnManipulationStarted(SpatialGestureRecognizer^ sender, SpatialManipulationStartedEventArgs^ args)
    {
      if (DetectIntersection(args->TryGetPointerPose(m_coordinateSystem)))
      {

      }
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnManipulationCanceled(SpatialGestureRecognizer^ sender, SpatialManipulationCanceledEventArgs^ args)
    {

    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnManipulationCompleted(SpatialGestureRecognizer^ sender, SpatialManipulationCompletedEventArgs^ args)
    {

    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnManipulationUpdated(SpatialGestureRecognizer^ sender, SpatialManipulationUpdatedEventArgs^ args)
    {
      auto delta = args->TryGetCumulativeDelta(m_coordinateSystem);
      auto translation = delta->Translation;
      translation.z = 0;
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnNavigationCompleted(SpatialGestureRecognizer^ sender, SpatialNavigationCompletedEventArgs^ args)
    {
      float3 delta = args->NormalizedOffset;
      if (delta.x < -NAVIGATION_THRESHOLD)
      {

      }
      else if (delta.x > NAVIGATION_THRESHOLD)
      {

      }
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnTapped(SpatialGestureRecognizer^ sender, SpatialTappedEventArgs^ args)
    {
      SpatialPointerPose^ pointerPose = args->TryGetPointerPose(m_coordinateSystem);
      if (pointerPose != nullptr)
      {

      }
    }

    //----------------------------------------------------------------------------
    void SpatialSourceHandler::OnSourceUpdated(SpatialInteractionSourceState^ state, SpatialCoordinateSystem^ coordinateSystem)
    {
      SpatialPointerPose^ pointerPose = state->TryGetPointerPose(coordinateSystem);
      if (pointerPose != nullptr)
      {
        DetectIntersection(pointerPose);
      }
    }

    //----------------------------------------------------------------------------
    bool SpatialSourceHandler::DetectIntersection(SpatialPointerPose^ pointerPose)
    {
      auto headPose = pointerPose->Head;
      const float3& forwardDirection = headPose->ForwardDirection;
      float targetDistance = 0.0f;

      //if (m_targetBoard->Intersects(headPose->Position, forwardDirection, &targetDistance))
      {
        // Compute the orientation based on the ray
        //const XMVECTOR orientationVector = DX::GetQuaternionVectorFromRay(forwardDirection, headPose->UpDirection);
        //XMStoreQuaternion(&m_toolPosition.Orientation, orientationVector);
      }

      return false;
    }
  }
}