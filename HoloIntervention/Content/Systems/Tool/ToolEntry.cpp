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
#include "Common.h"
#include "ToolEntry.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// STL includes
#include <string>
#include <sstream>

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace Tools
  {

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ToolEntry::GetStabilizedPosition() const
    {
      if (m_modelEntry != nullptr)
      {
        return transform(float3(0.f, 0.f, 0.f), m_modelEntry->GetWorld());
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ToolEntry::GetStabilizedNormal() const
    {
      if (m_modelEntry != nullptr)
      {
        return ExtractNormal(m_modelEntry->GetWorld());
      }
      return float3(0.f, 1.f, 0.f);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ToolEntry::GetStabilizedVelocity() const
    {
      if (m_modelEntry != nullptr)
      {
        return m_modelEntry->GetVelocity();
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float ToolEntry::GetStabilizePriority() const
    {
      // TODO : affect priority based on proximity to center of view frustrum?
      // TODO : stabilizer values?
      return m_isValid ? 2.5f : 0.75f;
    }

    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry(Rendering::ModelRenderer& modelRenderer, UWPOpenIGTLink::TransformName^ coordinateFrame, const std::wstring& modelName, UWPOpenIGTLink::TransformRepository^ transformRepository)
      : m_modelRenderer(modelRenderer)
      , m_transformRepository(transformRepository)
      , m_coordinateFrame(coordinateFrame)
    {
      CreateModel(modelName);

      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry(Rendering::ModelRenderer& modelRenderer, const std::wstring& coordinateFrame, const std::wstring& modelName, UWPOpenIGTLink::TransformRepository^ transformRepository)
      : m_modelRenderer(modelRenderer)
      , m_transformRepository(transformRepository)
    {
      m_coordinateFrame = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(coordinateFrame.c_str()));

      CreateModel(modelName);
    }

    //----------------------------------------------------------------------------
    ToolEntry::~ToolEntry()
    {
    }

    //----------------------------------------------------------------------------
    void ToolEntry::Update(const DX::StepTimer& timer)
    {
      // Transform repository has already been initialized with the transforms for this update

      bool isValid;
      float4x4 transform;
      try
      {
        transform = m_transformRepository->GetTransform(m_coordinateFrame, &isValid);
        m_isValid = isValid;
        m_modelEntry->RenderDefault();

        m_modelEntry->SetWorld(transform);
#if _DEBUG
        {
          static int x = 0;
          if (x == 0)
          {
            std::stringstream ss;
            ss << transform;
            HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_INFO, std::string("transform: ") + ss.str());
            x = 1;
          }
        }
#endif
      }
      catch (Platform::Exception^ e)
      {
        // Fail gracefully, it's possible that this transform wasn't available this frame
        m_modelEntry->RenderGreyscale();
        return;
      }
    }

    //----------------------------------------------------------------------------
    uint64 ToolEntry::GetId() const
    {
      if (m_modelEntry == nullptr)
      {
        return INVALID_TOKEN;
      }
      return m_modelEntry->GetId();
    }

    //----------------------------------------------------------------------------
    void ToolEntry::CreateModel(const std::wstring& modelName)
    {
      uint64 modelToken = m_modelRenderer.AddModel(L"Assets\\Models\\Tools\\" + modelName + L".cmo");
      if (modelToken == INVALID_TOKEN)
      {
        HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, std::wstring(L"Unable to create model with name: ") + modelName);
        return;
      }
      m_modelEntry = m_modelRenderer.GetModel(modelToken);
      m_modelEntry->SetVisible(false);
    }
  }
}