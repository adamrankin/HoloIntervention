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

// Local includes
#include "IRegistrationMethod.h"

// STL includes
#include <atomic>

namespace HoloIntervention
{
  namespace System
  {
    class NetworkSystem;
  }

  namespace Algorithm
  {
    class ManualRegistration : public IRegistrationMethod
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      virtual void SetWorldAnchor(Windows::Perception::Spatial::SpatialAnchor^ worldAnchor);

      virtual Concurrency::task<bool> StartAsync();
      virtual Concurrency::task<bool> StopAsync();
      virtual bool IsStarted();
      virtual void ResetRegistration();

      virtual void EnableVisualization(bool enabled);
      virtual void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ headPose,
                          Windows::Perception::Spatial::SpatialCoordinateSystem^ hmdCoordinateSystem,
                          Platform::IBox<Windows::Foundation::Numerics::float4x4>^ anchorToHMDBox);

      virtual Windows::Foundation::Numerics::float4x4 GetRegistrationTransformation() const;

    public:
      ManualRegistration(System::NetworkSystem& networkSystem);
      ~ManualRegistration();

    protected:
      // Cached systems
      System::NetworkSystem&                    m_networkSystem;

      // State variables
      std::wstring                              m_connectionName;
      uint64                                    m_hashedConnectionName;
      UWPOpenIGTLink::TransformRepository^      m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      Windows::Foundation::Numerics::float4x4   m_baselinePose = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4   m_baselineInverse = Windows::Foundation::Numerics::float4x4::identity();
      std::atomic_bool                          m_baselineNeeded = false;
      UWPOpenIGTLink::TransformName^            m_toolCoordinateFrameName = ref new UWPOpenIGTLink::TransformName(L"Tool", L"Reference");
      std::atomic_bool                          m_started = false;
      UWPOpenIGTLink::TrackedFrame^             m_frame = ref new UWPOpenIGTLink::TrackedFrame();
      double                                    m_latestTimestamp = 0.0;

      // Output
      Windows::Foundation::Numerics::float4x4   m_accumulatorMatrix = Windows::Foundation::Numerics::float4x4::identity();
      Windows::Foundation::Numerics::float4x4   m_registrationMatrix = Windows::Foundation::Numerics::float4x4::identity();
    };
  }
}