#include "pch.h"
#include "SpatialInputHandler.h"
#include <functional>

using namespace Windows::Foundation;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;

namespace TrackedUltrasound
{
  //----------------------------------------------------------------------------
  // Creates and initializes a GestureRecognizer that listens to a Person.
  SpatialInputHandler::SpatialInputHandler()
  {
    // The interaction manager provides an event that informs the app when
    // spatial interactions are detected.
    m_interactionManager = SpatialInteractionManager::GetForCurrentView();

    // Bind a handler to the SourcePressed event.
    m_sourcePressedEventToken =
      m_interactionManager->SourcePressed +=
        ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(
          bind( &SpatialInputHandler::OnSourcePressed, this, _1, _2 )
        );
  }

  //----------------------------------------------------------------------------
  SpatialInputHandler::~SpatialInputHandler()
  {
    // Unregister our event handlers
    m_interactionManager->SourcePressed -= m_sourcePressedEventToken;
    m_interactionManager->SourceDetected -= m_sourceDetectedEventToken;
  }

  //----------------------------------------------------------------------------
  // Checks if the user performed an input gesture since the last call to this method.
  // Allows the main update loop to check for asynchronous changes to the user
  // input state.
  SpatialInteractionSourceState^ SpatialInputHandler::CheckForPressedInput()
  {
    SpatialInteractionSourceState^ sourceState = m_sourceState;
    m_sourceState = nullptr;
    return sourceState;
  }

  //----------------------------------------------------------------------------
  void SpatialInputHandler::OnSourcePressed( SpatialInteractionManager^ sender, SpatialInteractionSourceEventArgs^ args )
  {
    m_sourceState = args->State;

    //
    // TODO: In your app or game engine, rewrite this method to queue
    //       input events in your input class or event handler.
    //
  }
}