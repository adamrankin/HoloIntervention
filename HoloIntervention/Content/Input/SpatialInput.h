/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "IEngineComponent.h"

// STL includes
#include <mutex>

namespace HoloIntervention
{
  namespace Input
  {
    class SpatialSourceHandler;

    class SpatialInput : public IEngineComponent
    {
    public:
      typedef std::function<void(uint32)> SourceCallbackFunc;

    public:
      SpatialInput();
      ~SpatialInput();

      void Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem);

      uint64 RegisterSourceObserver(SourceCallbackFunc detectedCallback, SourceCallbackFunc lostCallback, SourceCallbackFunc genericPressCallback);
      bool UnregisterSourceObserver(uint64 observerId);

      std::shared_ptr<SpatialSourceHandler> GetSourceHandlerById(uint32 sourceId);

    protected:
      void OnSourceDetected(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args);
      void OnSourceLost(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args);

      void OnSourcePressed(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args);
      void OnSourceUpdated(Windows::UI::Input::Spatial::SpatialInteractionManager^ sender, Windows::UI::Input::Spatial::SpatialInteractionSourceEventArgs^ args);

      std::shared_ptr<SpatialSourceHandler> GetFirstSourceHandlerByKind(Windows::UI::Input::Spatial::SpatialInteractionSourceKind kind);

    protected:
      Windows::UI::Input::Spatial::SpatialInteractionManager^     m_interactionManager = nullptr;

      Windows::Perception::Spatial::SpatialCoordinateSystem^      m_referenceFrame = nullptr;

      Windows::Foundation::EventRegistrationToken                 m_sourceLostEventToken;
      Windows::Foundation::EventRegistrationToken                 m_sourceDetectedEventToken;
      Windows::Foundation::EventRegistrationToken                 m_sourcePressedEventToken;
      Windows::Foundation::EventRegistrationToken                 m_sourceUpdatedEventToken;

      std::mutex                                                  m_sourceMutex;
      std::map<uint32, std::shared_ptr<SpatialSourceHandler>>     m_sourceMap;

      std::map<uint64, SourceCallbackFunc>                        m_sourceDetectedObservers;
      std::map<uint64, SourceCallbackFunc>                        m_sourceLostObservers;
      std::map<uint64, SourceCallbackFunc>                        m_genericPressObservers;
      uint64                                                      m_nextSourceObserverId = INVALID_TOKEN + 1;
    };
  }
}