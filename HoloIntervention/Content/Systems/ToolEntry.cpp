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
#include "ToolEntry.h"
#include "AppView.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// std includes
#include <string>

namespace HoloIntervention
{
  namespace Tools
  {
    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry( const TransformName& coordinateFrame, const std::wstring& modelName )
    {
      m_coordinateFrame = coordinateFrame;

      CreateModel(modelName);
    }

    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry( const std::wstring& coordinateFrame, const std::wstring& modelName )
    {
      m_coordinateFrame = coordinateFrame;

      CreateModel(modelName);
    }

    //----------------------------------------------------------------------------
    ToolEntry::~ToolEntry()
    {
    }

    //----------------------------------------------------------------------------
    void ToolEntry::Update(const DX::StepTimer& timer, UWPOpenIGTLink::TrackedFrame^ frame)
    {

    }

    //----------------------------------------------------------------------------
    uint64 ToolEntry::GetId() const
    {
      if ( m_modelEntry == nullptr )
      {
        return Rendering::INVALID_MODEL_ENTRY;
      }
      return m_modelEntry->GetId();
    }

    //----------------------------------------------------------------------------
    void ToolEntry::CreateModel(const std::wstring& modelName)
    {
      uint64 modelToken = HoloIntervention::instance()->GetModelRenderer().AddModel(L"Assets\\Models\\Tools\\" + modelName + L".cmo");
      if (modelToken == Rendering::INVALID_MODEL_ENTRY)
      {
        std::wstring error(L"Unable to create model with name: ");
        error += modelName;
        OutputDebugStringW(error.c_str());
        return;
      }
      m_modelEntry = HoloIntervention::instance()->GetModelRenderer().GetModel(modelToken);
    }
  }
}