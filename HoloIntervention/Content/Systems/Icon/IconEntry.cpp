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
#include "IconEntry.h"

// Rendering includes
#include "ModelEntry.h"

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    IconEntry::IconEntry()
    {
    }

    //----------------------------------------------------------------------------
    IconEntry::~IconEntry()
    {
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    uint64 IconEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetScaleFactor(float scaleFactor)
    {
      m_scaleFactor = scaleFactor;
    }

    //----------------------------------------------------------------------------
    float IconEntry::GetScaleFactor() const
    {
      return m_scaleFactor;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Rendering::ModelEntry> IconEntry::GetModelEntry() const
    {
      return m_modelEntry;
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetModelEntry(std::shared_ptr<Rendering::ModelEntry> entry)
    {
      m_modelEntry = entry;
    }

    //----------------------------------------------------------------------------
    bool IconEntry::GetFirstFrame() const
    {
      return m_firstFrame;
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetFirstFrame(bool firstFrame)
    {
      m_firstFrame = firstFrame;
    }

    //----------------------------------------------------------------------------
    uint64 IconEntry::GetUserValueNumber() const
    {
      return m_userValueNumber;
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetUserValue(uint64 userValue)
    {
      m_userValueNumber = userValue;
    }

    //----------------------------------------------------------------------------
    void IconEntry::SetUserValue(const std::wstring& userValue)
    {
      m_userValueString = userValue;
    }

    //----------------------------------------------------------------------------
    std::wstring IconEntry::GetUserValueString() const
    {
      return m_userValueString;
    }

  }
}