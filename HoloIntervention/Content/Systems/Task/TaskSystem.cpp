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
#include "StepTimer.h"
#include "TaskSystem.h"

// Task includes
#include "PreOpImageTask.h"
#include "TouchingSphereTask.h"

using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace concurrency;

namespace HoloIntervention
{
  namespace System
  {
    //-----------------------------------------------------------------------------
    task<bool> TaskSystem::WriteConfigurationAsync(XmlDocument^ document)
    {
      auto preopTask = m_preopImageTask->WriteConfigurationAsync(document);
      auto touchingTask = m_touchingSphereTask->WriteConfigurationAsync(document);
      auto tasks = { preopTask, touchingTask };
      return when_all(begin(tasks), end(tasks)).then([this](const std::vector<bool>& results)
      {
        return results[0] && results[1];
      });
    }

    //-----------------------------------------------------------------------------
    task<bool> TaskSystem::ReadConfigurationAsync(XmlDocument^ document)
    {
      auto preopTask = m_preopImageTask->ReadConfigurationAsync(document);
      auto touchingTask = m_touchingSphereTask->ReadConfigurationAsync(document);
      auto tasks = { preopTask, touchingTask};
      return when_all(begin(tasks), end(tasks)).then([this](const std::vector<bool>& results)
      {
        m_componentReady = results[0] && results[1];
        return results[0] && results[1];
      });
    }

    //-----------------------------------------------------------------------------
    float3 TaskSystem::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      return m_touchingSphereTask->GetStabilizePriority() > m_preopImageTask->GetStabilizePriority() ? m_touchingSphereTask->GetStabilizedPosition(pose) : m_preopImageTask->GetStabilizedPosition(pose);
    }

    //-----------------------------------------------------------------------------
    float3 TaskSystem::GetStabilizedVelocity() const
    {
      return m_touchingSphereTask->GetStabilizePriority() > m_preopImageTask->GetStabilizePriority() ? m_touchingSphereTask->GetStabilizedVelocity() : m_preopImageTask->GetStabilizedVelocity();
    }

    //-----------------------------------------------------------------------------
    float TaskSystem::GetStabilizePriority() const
    {
      return std::fmax(m_touchingSphereTask->GetStabilizePriority(), m_preopImageTask->GetStabilizePriority());
    }

    //-----------------------------------------------------------------------------
    void TaskSystem::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      m_touchingSphereTask->RegisterVoiceCallbacks(callbackMap);
      m_preopImageTask->RegisterVoiceCallbacks(callbackMap);
    }

    //----------------------------------------------------------------------------
    TaskSystem::TaskSystem(NotificationSystem& notificationSystem, NetworkSystem& networkSystem, RegistrationSystem& registrationSystem, Rendering::ModelRenderer& modelRenderer)
      : m_notificationSystem(notificationSystem)
      , m_networkSystem(networkSystem)
      , m_registrationSystem(registrationSystem)
      , m_modelRenderer(modelRenderer)
    {
      m_touchingSphereTask = std::make_shared<Tasks::TouchingSphereTask>(notificationSystem, networkSystem, registrationSystem, modelRenderer);
      m_preopImageTask = std::make_shared<Tasks::PreOpImageTask>(notificationSystem, networkSystem, registrationSystem, modelRenderer);
    }

    //----------------------------------------------------------------------------
    TaskSystem::~TaskSystem()
    {
      m_touchingSphereTask = nullptr;
      m_preopImageTask = nullptr;
    }

    //-----------------------------------------------------------------------------
    void TaskSystem::Update(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, DX::StepTimer& stepTimer)
    {
      m_touchingSphereTask->Update(coordinateSystem, stepTimer);
      m_preopImageTask->Update(coordinateSystem, stepTimer);
    }
  }
}