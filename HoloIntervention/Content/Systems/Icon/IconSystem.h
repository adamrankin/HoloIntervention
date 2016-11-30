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
#include "IEngineComponent.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace System
  {
    class IconEntry;

    typedef std::vector<std::shared_ptr<IconEntry>> IconEntryList;

    class IconSystem : public IEngineComponent
    {
    public:
      IconSystem();
      ~IconSystem();

      void Update(DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ renderingCoordinateSystem);

      std::shared_ptr<IconEntry> AddEntry(const std::wstring& modelName);
      bool RemoveEntry(uint64 entryId);
      std::shared_ptr<IconEntry> GetEntry(uint64 entryId);

    protected:
      uint64                      m_nextValidEntry = 0;
      IconEntryList               m_iconEntries;

      // Icons that this subsystem manages
      std::shared_ptr<IconEntry>  m_networkIcon = nullptr;
      std::shared_ptr<IconEntry>  m_cameraIcon = nullptr;
    };
  }
}