
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

#include <atomic>

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelEntry;
  }

  namespace UI
  {
    class IconEntry
    {
    public:
      IconEntry();
      ~IconEntry();

      void SetId(uint64 id);
      uint64 GetId() const;

      void SetScaleFactor(float scaleFactor);
      float GetScaleFactor() const;

      std::shared_ptr<Rendering::ModelEntry> GetModelEntry() const;
      void SetModelEntry(std::shared_ptr<Rendering::ModelEntry> entry);

      bool GetFirstFrame() const;
      void SetFirstFrame(bool firstFrame);

      void SetUserValue(uint64 userValue);
      uint64 GetUserValueNumber() const;
      void SetUserValue(const std::wstring& userValue);
      std::wstring GetUserValueString() const;

    protected:
      uint64                                          m_id;
      std::atomic_bool                                m_firstFrame = true;
      std::shared_ptr<Rendering::ModelEntry>          m_modelEntry;
      float                                           m_scaleFactor = 1.f;

      uint64                                          m_userValueNumber = 0;
      std::wstring                                    m_userValueString = L"";
    };
  }
}