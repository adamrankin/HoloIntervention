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
#include "ManualRegistration.h"

// System includes
#include "NetworkSystem.h"

// STL includes
#include <sstream>

// Intellisense
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace Platform;
using namespace Windows::Perception::Spatial;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ManualRegistration::GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const
    {
      return Windows::Foundation::Numerics::float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ManualRegistration::GetStabilizedNormal(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const
    {
      return Windows::Foundation::Numerics::float3(0.f, 1.f, 0.f);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ManualRegistration::GetStabilizedVelocity() const
    {
      return Windows::Foundation::Numerics::float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    task<bool> ManualRegistration::WriteConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (document->SelectNodes(L"/HoloIntervention")->Length != 1)
        {
          return false;
        }

        auto rootNode = document->SelectNodes(L"/HoloIntervention")->Item(0);

        auto camRegElem = document->CreateElement(L"ManualRegistration");
        camRegElem->SetAttribute(L"IGTConnection", ref new Platform::String(m_connectionName.c_str()));
        camRegElem->SetAttribute(L"FromFrameName", m_toolCoordinateFrameName->From());
        camRegElem->SetAttribute(L"ToFrameName", m_toolCoordinateFrameName->To());
        rootNode->AppendChild(camRegElem);

        return true;
      });
    }

    //----------------------------------------------------------------------------
    task<bool> ManualRegistration::ReadConfigurationAsync(XmlDocument^ document)
    {
      return create_task([this, document]()
      {
        if (!m_transformRepository->ReadConfiguration(document))
        {
          return false;
        }

        auto xpath = ref new Platform::String(L"/HoloIntervention/ManualRegistration");
        if (document->SelectNodes(xpath)->Length == 0)
        {
          throw ref new Platform::Exception(E_INVALIDARG, L"No manual registration defined in the configuration file.");
        }

        auto node = document->SelectNodes(xpath)->Item(0);

        if (!HasAttribute(L"IGTConnection", node))
        {
          throw ref new Platform::Exception(E_FAIL, L"Manual registration entry does not contain \"IGTConnection\" attribute.");
        }
        Platform::String^ igtConnectionName = dynamic_cast<Platform::String^>(node->Attributes->GetNamedItem(L"IGTConnection")->NodeValue);
        m_connectionName = std::wstring(igtConnectionName->Data());
        m_hashedConnectionName = HashString(igtConnectionName);

        std::wstring fromFrameName;
        std::wstring toFrameName;
        if (!GetAttribute(L"FromFrameName", node, fromFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"FromFrameName attribute not defined for manual registration. Aborting.");
          return false;
        }
        if (!GetAttribute(L"ToFrameName", node, toFrameName))
        {
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"ToFrameName attribute not defined for manual registration. Aborting.");
          return false;
        }
        m_toolCoordinateFrameName = ref new UWPOpenIGTLink::TransformName(
                                      ref new Platform::String(fromFrameName.c_str()),
                                      ref new Platform::String(toFrameName.c_str())
                                    );

        return true;
      });
    }

    //----------------------------------------------------------------------------
    void ManualRegistration::SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor)
    {

    }

    //----------------------------------------------------------------------------
    ManualRegistration::ManualRegistration(System::NetworkSystem& networkSystem)
      : m_networkSystem(networkSystem)
    {
    }

    //----------------------------------------------------------------------------
    ManualRegistration::~ManualRegistration()
    {
    }

    //----------------------------------------------------------------------------
    task<bool> ManualRegistration::StartAsync()
    {
      m_baselineNeeded = true;
      m_started = true;
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    task<bool> ManualRegistration::StopAsync()
    {
      m_started = false;
      return task_from_result(true);
    }

    //----------------------------------------------------------------------------
    bool ManualRegistration::IsStarted()
    {
      return m_started;
    }

    //----------------------------------------------------------------------------
    void ManualRegistration::ResetRegistration()
    {
      m_registrationMatrixInverse = float4x4::identity();
      m_baselineNeeded = true;
    }

    //----------------------------------------------------------------------------
    void ManualRegistration::EnableVisualization(bool enabled)
    {

    }

    //----------------------------------------------------------------------------
    void ManualRegistration::Update(SpatialPointerPose^ headPose, SpatialCoordinateSystem^ hmdCoordinateSystem, IBox<float4x4>^ anchorToHMDBox)
    {
      if (!m_started)
      {
        return;
      }

      // grab latest network frame
      auto transformFrame = m_networkSystem.GetTransformFrame(m_hashedConnectionName, m_latestTimestamp);
      if (transformFrame == nullptr)
      {
        return;
      }
      m_transformRepository->SetTransforms(transformFrame);

      auto opticalPose = float4x4::identity();
      if (!m_transformRepository->GetTransform(m_toolCoordinateFrameName, &opticalPose))
      {
        return;
      }

      if (m_baselineNeeded)
      {
        m_baselinePose = transpose(opticalPose);
        if (!invert(m_baselinePose, &m_baselineInverse))
        {
          m_baselineNeeded = true;
          LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to invert pose transformation. How is this possible?");
          return;
        }
        m_baselineNeeded = false;
        return;
      }

      m_registrationMatrixInverse = opticalPose * m_baselineInverse;
    }

    //----------------------------------------------------------------------------
    float4x4 ManualRegistration::GetRegistrationTransformation() const
    {
      float4x4 result;
      if (!invert(m_registrationMatrixInverse, &result))
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, L"Unable to invert pose transformation. How is this possible?");
        return float4x4::identity();
      }
      return result;
    }
  }
}