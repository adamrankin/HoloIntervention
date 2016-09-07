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
#include "ToolEntry.h"

// std includes

// WinRT includes

namespace HoloIntervention
{
  class TransformName;

  namespace Tools
  {
    class ToolSystem
    {
    public:
      ToolSystem();
      ~ToolSystem();

      uint64 RegisterTool( const std::wstring& modelName, UWPOpenIGTLink::TransformName^ coordinateFrame );
      void UnregisterTool( uint64 toolToken );
      void ClearTools();

      void Update(const DX::StepTimer& timer, UWPOpenIGTLink::TrackedFrame^ frame);

    protected:
      std::vector<ToolEntry>            m_toolEntries;
    };
  }
}