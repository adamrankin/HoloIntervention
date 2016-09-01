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
#include "pch.h"
#include "ToolSystem.h"
#include "AppView.h"

// Rendering includes
#include "ModelRenderer.h"

// STL includes

// WinRT includes

namespace HoloIntervention
{
  namespace Tools
  {
    //----------------------------------------------------------------------------
    ToolSystem::ToolSystem()
    {
    }

    //----------------------------------------------------------------------------
    ToolSystem::~ToolSystem()
    {
    }

    //----------------------------------------------------------------------------
    uint64 ToolSystem::RegisterTool( const std::wstring& modelName, const std::wstring& coordinateFrame )
    {
      // TODO : Determine filename associated with this model
      HoloIntervention::instance()->GetModelRenderer().AddModel(modelName);
    }

    //----------------------------------------------------------------------------
    void ToolSystem::UnregisterTool( uint64 toolToken )
    {
      for_each ( m_modelEntries.begin(), m_modelEntries.end(), [ = ]( auto iter )
      {
        if ( ( *iter )->GetId() == toolToken )
        {
          m_modelTokens.erase( std::find( m_modelTokens.begin(), m_modelTokens.end(), toolToken ) );
          m_modelEntries.erase( iter );
          return;
        }
      } );
    }

    //----------------------------------------------------------------------------
    void ToolSystem::ClearTools()
    {
      m_modelTokens.clear();
      m_modelEntries.clear();
    }
  }
}