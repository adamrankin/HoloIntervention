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

// STL includes
#include <vector>

// Local includes
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

// WinRT includes
#include <ppltasks.h>

// Registration method includes
#include "CameraRegistration.h"

namespace DX
{
  struct ViewProjectionConstantBuffer;
}

namespace HoloIntervention
{
  namespace Physics
  {
    class SurfaceAPI;
  }
  namespace Rendering
  {
    class ModelEntry;
  }
  namespace Network
  {
    class IGTConnector;
  }

  namespace System
  {
    class NotificationSystem;

    class RegistrationSystem : public Sound::IVoiceInput, public IStabilizedComponent
    {
    public:
      // IStabilizedComponent methods
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedNormal() const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      RegistrationSystem(Network::IGTConnector& igtConnector,
                         Physics::SurfaceAPI& physicsAPI,
                         NotificationSystem& notificationSystem,
                         Rendering::ModelRenderer& modelRenderer);
      ~RegistrationSystem();

      void Update(DX::StepTimer& timer,
                  Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem,
                  Windows::UI::Input::Spatial::SpatialPointerPose^ headPose);

      Concurrency::task<void> LoadAppStateAsync();
      bool IsCameraActive() const;
      virtual void RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks);

      bool GetReferenceToCoordinateSystemTransformation(Windows::Perception::Spatial::SpatialCoordinateSystem^ coordinateSystem, Windows::Foundation::Numerics::float4x4& outTransform);

    protected:
      // Cached references
      NotificationSystem&                             m_notificationSystem;
      Rendering::ModelRenderer&                       m_modelRenderer;
      Physics::SurfaceAPI&                            m_physicsAPI;

      // Anchor variables
      std::atomic_bool                                m_registrationActive = false;
      std::atomic_bool                                m_regAnchorRequested = false;
      uint64_t                                        m_regAnchorModelId = 0;
      std::shared_ptr<Rendering::ModelEntry>          m_regAnchorModel = nullptr;
      Windows::Perception::Spatial::SpatialAnchor^    m_regAnchor = nullptr;

      // Registration methods
      std::shared_ptr<CameraRegistration>             m_cameraRegistration;
      Windows::Foundation::Numerics::float4x4         m_cachedRegistrationTransform = Windows::Foundation::Numerics::float4x4::identity();

      // Constants
      static Platform::String^                        REGISTRATION_ANCHOR_NAME;
      static const std::wstring                       REGISTRATION_ANCHOR_MODEL_FILENAME;
    };
  }
}