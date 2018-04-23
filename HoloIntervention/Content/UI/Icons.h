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
#include "IConfigurable.h"
#include "IStabilizedComponent.h"
#include "Icon.h"

// Rendering includes
#include "Content/Rendering/Model/Model.h"

namespace DX
{
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
  }

  namespace UI
  {
    typedef std::vector<std::shared_ptr<Icon>> IconEntryList;

    class Icons : public IStabilizedComponent
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      Icons(Rendering::ModelRenderer& modelRenderer);
      ~Icons();

      void Update(DX::StepTimer& timer, Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      Concurrency::task<std::shared_ptr<Icon>> AddEntryAsync(const std::wstring& modelName, std::wstring userValue = L"");
      Concurrency::task<std::shared_ptr<Icon>> AddEntryAsync(const std::wstring& modelName, uint64 userValue = 0);
      Concurrency::task<std::shared_ptr<Icon>> AddEntryAsync(std::shared_ptr<Rendering::Model> modelEntry, std::wstring userValue = L"");
      Concurrency::task<std::shared_ptr<Icon>> AddEntryAsync(std::shared_ptr<Rendering::Model> modelEntry, uint64 userValue = 0);
      bool RemoveEntry(uint64 entryId);
      std::shared_ptr<Icon> GetEntry(uint64 entryId);

    protected:
      std::mutex                                m_entryMutex;
      uint64                                    m_nextValidEntry = 0;
      IconEntryList                             m_iconEntries;
      std::atomic_bool                          m_iconsShowing = true;

      // Cached entries to model renderer
      Rendering::ModelRenderer&                 m_modelRenderer;

      // Shared variables
      static const float                        ANGLE_BETWEEN_ICONS_RAD;
      static const float                        ICON_START_ANGLE;
      static const float                        ICON_UP_ANGLE;
      static const float                        ICON_SIZE_METER;
    };
  }
}