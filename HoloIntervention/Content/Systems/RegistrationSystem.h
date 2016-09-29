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

// std includes
#include <vector>

// Sound includes
#include "IVoiceInput.h"

// WinRT includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Networking::Sockets;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;

namespace NetworkPCL
{
  enum PCLMessageType
  {
    NetworkPCL_POINT_DATA,
    NetworkPCL_REGISTRATION_RESULT,
    NetworkPCL_KEEP_ALIVE
  };

  struct PCLMessageHeader
  {
    uint16_t  messageType;
    uint32_t  additionalHeaderSize;
    uint32_t  bodySize;
    uint32_t  referenceVertexCount;
    uint32_t  targetVertexCount;
  };
}

namespace HoloIntervention
{
  namespace Spatial
  {
    class SurfaceMesh;
  }

  namespace Rendering
  {
    class ModelEntry;
  }

  namespace System
  {
    class RegistrationSystem : public Sound::IVoiceInput
    {
    public:
      RegistrationSystem( const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer );
      ~RegistrationSystem();

      void Update( SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose );

      task<void> LoadAppStateAsync();

      virtual void RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg );

      // Send the collected points and mesh data to the NetworkPCL interface
      task<bool> SendRegistrationDataAsync();
      task<float4x4> WaitForRegistrationResultAsync();

      float4x4 GetRegistrationResult();

    protected:
      task<void> DataReceiverAsync();

    protected:
      // Keep a reference to the device resources
      std::shared_ptr<DX::DeviceResources>      m_deviceResources;
      DX::StepTimer&                            m_stepTimer;

      // Anchor behavior variables
      bool                                      m_regAnchorRequested = false;
      uint64_t                                  m_regAnchorModelId = 0;
      std::shared_ptr<Rendering::ModelEntry>    m_regAnchorModel = nullptr;

      // NetworkPCL related variables
      StreamSocket^                             m_networkPCLSocket = ref new StreamSocket();
      bool                                      m_connected = false;
      NetworkPCL::PCLMessageHeader              m_nextHeader;
      cancellation_token_source                 m_tokenSource;
      task<void>*                               m_receiverTask = nullptr;
      bool                                      m_registrationResultReceived = false;
      float4x4                                  m_registrationResult = float4x4::identity();

      // Point collection behavior variables
      bool                                      m_collectingPoints = false;
      UWPOpenIGTLink::TrackedFrame^             m_trackedFrame = ref new UWPOpenIGTLink::TrackedFrame();
      UWPOpenIGTLink::TransformRepository^      m_transformRepository = ref new UWPOpenIGTLink::TransformRepository();
      UWPOpenIGTLink::TransformName^            m_stylusTipToReferenceName = ref new UWPOpenIGTLink::TransformName( L"StylusTip", L"Reference" );
      double                                    m_latestTimestamp = 0;
      std::vector<float3>                       m_points;
      std::shared_ptr<Spatial::SurfaceMesh>     m_spatialMesh = nullptr;

      static Platform::String^                  ANCHOR_NAME;
      static const std::wstring                 ANCHOR_MODEL_FILENAME;
    };
  }
}