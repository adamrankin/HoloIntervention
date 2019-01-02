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

// Local includes
#include "pch.h"
#include "NetworkSystem.h"
#include "Tool.h"

// Valhalla includes
#include <Common\Common.h>
#include <Rendering\Model\Model.h>
#include <Rendering\Model\ModelRenderer.h>
#include <UI\Icons.h>

// STL includes
#include <string>
#include <sstream>

// OpenCV includes
#include <opencv2/core.hpp>

using namespace Concurrency;
using namespace Valhalla;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace cv;

namespace HoloIntervention
{
  namespace Tools
  {
    Platform::String^ Tool::MODEL_COORDINATE_FRAME_NAME = L"Model";

    //----------------------------------------------------------------------------
    float3 Tool::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      if(m_modelEntry != nullptr && m_modelEntry->IsLoaded())
      {
        return transform(float3(0.f, 0.f, 0.f), m_modelEntry->GetCurrentPose());
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 Tool::GetStabilizedVelocity() const
    {
      if(m_modelEntry != nullptr && m_modelEntry->IsLoaded())
      {
        return m_modelEntry->GetVelocity();
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float Tool::GetStabilizePriority() const
    {
      if(!m_modelEntry->IsLoaded() || !m_modelEntry->IsInFrustum())
      {
        return PRIORITY_NOT_ACTIVE;
      }

      return m_wasValid ? PRIORITY_VALID_TOOL : PRIORITY_INVALID_TOOL;
    }

    //----------------------------------------------------------------------------
    Tool::Tool(Rendering::ModelRenderer& modelRenderer,
               System::NetworkSystem& networkSystem,
               UI::Icons& icons,
               uint64 hashedConnectionName,
               UWPOpenIGTLink::TransformName^ coordinateFrame,
               UWPOpenIGTLink::TransformRepository^ transformRepository,
               Platform::String^ userId)
      : m_modelRenderer(modelRenderer)
      , m_networkSystem(networkSystem)
      , m_icons(icons)
      , m_hashedConnectionName(hashedConnectionName)
      , m_transformRepository(transformRepository)
      , m_coordinateFrame(coordinateFrame)
      , m_userId(std::wstring(userId->Data()))
    {
      m_modelCoordinateFrameName = MODEL_COORDINATE_FRAME_NAME + userId;
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    Tool::Tool(Rendering::ModelRenderer& modelRenderer,
               System::NetworkSystem& networkSystem,
               UI::Icons& icons,
               uint64 hashedConnectionName,
               const std::wstring& coordinateFrame,
               UWPOpenIGTLink::TransformRepository^ transformRepository,
               Platform::String^ userId)
      : m_modelRenderer(modelRenderer)
      , m_networkSystem(networkSystem)
      , m_icons(icons)
      , m_hashedConnectionName(hashedConnectionName)
      , m_transformRepository(transformRepository)
      , m_userId(std::wstring(userId->Data()))
    {
      m_modelCoordinateFrameName = MODEL_COORDINATE_FRAME_NAME + userId;
      m_coordinateFrame = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(coordinateFrame.c_str()));
    }

    //----------------------------------------------------------------------------
    Tool::~Tool()
    {
    }

    //----------------------------------------------------------------------------
    void Tool::Update(const DX::StepTimer& timer)
    {
      bool registrationTransformValid = m_transformRepository->GetTransformValid(ref new UWPOpenIGTLink::TransformName(m_coordinateFrame->To(), HOLOLENS_COORDINATE_SYSTEM_PNAME));
      if(!registrationTransformValid)
      {
        m_modelEntry->SetVisible(false);
        if(m_iconEntry != nullptr)
        {
          m_iconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        }
        return;
      }
      else
      {
        m_modelEntry->SetVisible(!m_hiddenOverride);
      }

      // m_transformRepository has already been initialized with the network transforms for this update
      auto objectToRefTransform = m_networkSystem.GetTransform(m_hashedConnectionName, m_coordinateFrame, m_latestTimestamp);

      if(objectToRefTransform == nullptr)
      {
        // No new transform since last timestamp
        return;
      }

      m_latestTimestamp = objectToRefTransform->Timestamp;
      m_transformRepository->SetTransform(m_coordinateFrame, objectToRefTransform->Matrix, objectToRefTransform->Valid);

      IKeyValuePair<bool, float4x4>^ result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(GetModelCoordinateFrameName(), HOLOLENS_COORDINATE_SYSTEM_PNAME));
      m_isValid = result->Key;
      if(!m_isValid && m_wasValid)
      {
        m_wasValid = false;
        if(m_modelEntry != nullptr)
        {
          m_modelEntry->RenderGreyscale();
        }
        if(m_iconEntry != nullptr)
        {
          m_iconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        }
        return;
      }

      if(m_isValid)
      {
        if(!m_wasValid)
        {
          if(m_iconEntry != nullptr)
          {
            m_iconEntry->GetModel()->SetRenderingState(Rendering::RENDERING_DEFAULT);
          }
          if(m_modelEntry != nullptr)
          {
            m_modelEntry->RenderDefault();
          }
        }

        if(m_modelEntry != nullptr)
        {
          m_modelEntry->SetDesiredPose(transpose(result->Value));
        }
        m_wasValid = true;
      }
    }

    //----------------------------------------------------------------------------
    task<void> Tool::SetModelAsync(std::shared_ptr<Rendering::Model> entry)
    {
      return create_task([this, entry]()
      {
        if(m_modelEntry == entry)
        {
          return;
        }

        ShowIcon(false);

        m_modelEntry = entry;
      });
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Rendering::Model> Tool::GetModel()
    {
      return m_modelEntry;
    }

    //----------------------------------------------------------------------------
    void Tool::SetCoordinateFrame(UWPOpenIGTLink::TransformName^ coordFrame)
    {
      m_coordinateFrame = coordFrame;
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(GetModelCoordinateFrameName(), m_coordinateFrame->From()), transpose(m_modelToObjectTransform), true);
      m_transformRepository->SetTransformPersistent(ref new UWPOpenIGTLink::TransformName(GetModelCoordinateFrameName(), m_coordinateFrame->From()), true);
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TransformName^ Tool::GetCoordinateFrame() const
    {
      return m_coordinateFrame;
    }

    //----------------------------------------------------------------------------
    bool Tool::IsValid() const
    {
      return m_isValid;
    }

    //----------------------------------------------------------------------------
    bool Tool::WasValid() const
    {
      return m_wasValid;
    }

    //----------------------------------------------------------------------------
    void Tool::SetModelToObjectTransform(Windows::Foundation::Numerics::float4x4 transform)
    {
      if(transform.m41 == 0 && transform.m41 == 0 && transform.m42 == 0 && (transform.m14 != 0 || transform.m24 != 0 || transform.m34 != 0))
      {
        // Convert to column major
        transform = transpose(transform);
      }
      m_modelToObjectTransform = transform;

      // Store as row-major (UWPOpenIGTLink convention)
      m_transformRepository->SetTransform(ref new UWPOpenIGTLink::TransformName(GetModelCoordinateFrameName(), m_coordinateFrame->From()), transpose(m_modelToObjectTransform), true);
      m_transformRepository->SetTransformPersistent(ref new UWPOpenIGTLink::TransformName(GetModelCoordinateFrameName(), m_coordinateFrame->From()), true);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 Tool::GetModelToObjectTransform() const
    {
      return m_modelToObjectTransform;
    }

    //----------------------------------------------------------------------------
    uint64 Tool::GetId() const
    {
      if(m_modelEntry == nullptr)
      {
        return INVALID_TOKEN;
      }
      return m_modelEntry->GetId();
    }

    //----------------------------------------------------------------------------
    std::wstring Tool::GetUserId() const
    {
      return m_userId;
    }

    //----------------------------------------------------------------------------
    void Tool::SetHiddenOverride(bool arg)
    {
      m_hiddenOverride = arg;
    }

    //----------------------------------------------------------------------------
    void Tool::ShowIcon(bool show)
    {
      if(show && m_iconEntry == nullptr)
      {
        m_icons.AddEntryAsync(m_modelEntry, 0).then([this](std::shared_ptr<UI::Icon> iconEntry)
        {
          m_iconEntry = iconEntry;
          iconEntry->GetModel()->SetVisible(true);
        });
      }
      else if(!show && m_iconEntry != nullptr)
      {
        m_icons.RemoveEntry(m_iconEntry->GetId());
        m_iconEntry = nullptr;
      }
    }

    //----------------------------------------------------------------------------
    Platform::String^ Tool::GetModelCoordinateFrameName()
    {
      return m_modelCoordinateFrameName;
    }
  }
}
