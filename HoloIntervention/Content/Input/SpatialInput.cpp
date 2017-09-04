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
#include "SpatialInput.h"
#include "SpatialSourceHandler.h"

// STL includes
#include <functional>

using namespace Windows::Foundation;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Input
  {
    //----------------------------------------------------------------------------
    SpatialInput::SpatialInput()
    {
      m_interactionManager = SpatialInteractionManager::GetForCurrentView();

      m_sourceDetectedEventToken =
        m_interactionManager->SourceDetected +=
          ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(
            std::bind(&SpatialInput::OnSourceDetected, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_sourceLostEventToken =
        m_interactionManager->SourceLost +=
          ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(
            std::bind(&SpatialInput::OnSourceLost, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_sourcePressedEventToken =
        m_interactionManager->SourcePressed +=
          ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(
            std::bind(&SpatialInput::OnSourcePressed, this, std::placeholders::_1, std::placeholders::_2)
          );

      // Source Updated is raised when the input state or the location of a source changes.
      // The main usage is to display the tool associated to a controller at the right position
      m_sourceUpdatedEventToken =
        m_interactionManager->SourceUpdated +=
          ref new TypedEventHandler<SpatialInteractionManager^, SpatialInteractionSourceEventArgs^>(
            std::bind(&SpatialInput::OnSourceUpdated, this, std::placeholders::_1, std::placeholders::_2)
          );

      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    SpatialInput::~SpatialInput()
    {
      m_interactionManager->SourceLost -= m_sourceLostEventToken;
      m_interactionManager->SourceDetected -= m_sourceDetectedEventToken;
      m_interactionManager->SourcePressed -= m_sourcePressedEventToken;
      m_interactionManager->SourceUpdated -= m_sourceUpdatedEventToken;
    }

    //----------------------------------------------------------------------------
    void SpatialInput::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      m_referenceFrame = coordinateSystem;
    }

    //----------------------------------------------------------------------------
    void SpatialInput::OnSourceDetected(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args)
    {
      SpatialInteractionSourceState^ state = args->State;
      SpatialInteractionSource^ source = state->Source;

      if (source->Kind == SpatialInteractionSourceKind::Hand)
      {
        auto it = m_sourceMap.find(source->Id);
        if (it == m_sourceMap.end())
        {
          return;
        }

        std::shared_ptr<SpatialSourceHandler> sourceHandler = std::make_shared<SpatialSourceHandler>(source);
        sourceHandler->OnSourceUpdated(state, m_referenceFrame);
        m_sourceMap[source->Id] = sourceHandler;
      }
    }

    //----------------------------------------------------------------------------
    void SpatialInput::OnSourceLost(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args)
    {
      auto it = m_sourceMap.find(args->State->Source->Id);
      if (it != m_sourceMap.end())
      {
        m_sourceMap.erase(it);
      }
    }

    //----------------------------------------------------------------------------
    void SpatialInput::OnSourcePressed(SpatialInteractionManager^ sender, SpatialInteractionSourceEventArgs^ args)
    {
      SpatialInteractionSourceState^ state = args->State;
      SpatialInteractionSource^ source = state->Source;
      SpatialSourceHandler* sourceHandler = GetSourceHandler(source->Id);
      if (sourceHandler != nullptr)
      {
        sourceHandler->OnSourcePressed(args);
      }
    }

    //----------------------------------------------------------------------------
    void SpatialInput::OnSourceUpdated(SpatialInteractionManager^ sender, SpatialInteractionSourceEventArgs^ args)
    {
      SpatialInteractionSourceState^ state = args->State;
      SpatialInteractionSource^ source = state->Source;
      SpatialSourceHandler* sourceHandler = GetSourceHandler(source->Id);
      if (sourceHandler != nullptr)
      {
        sourceHandler->OnSourceUpdated(state, m_referenceFrame);
      }
    }

    //----------------------------------------------------------------------------
    HoloIntervention::Input::SpatialSourceHandler* SpatialInput::GetSourceHandler(unsigned int sourceId)
    {
      auto it = m_sourceMap.find(sourceId);
      return (it == m_sourceMap.end()) ? nullptr : it->second.get();
    }
  }
}