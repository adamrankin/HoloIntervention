//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#pragma once

// Local includes
#include "Interfaces\IEngineComponent.h"
#include "Input\IVoiceInput.h"

// STL includes
#include <memory>
#include <map>

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace Valhalla
{
  namespace Physics
  {
    class PhysicsAPI;
  }

  namespace Rendering
  {
    class MeshRenderer : public IEngineComponent, public Input::IVoiceInput
    {
    public:
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);

    public:
      MeshRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources, Physics::PhysicsAPI& physics);

      void Render();

      void SetEnabled(bool arg);
      bool GetEnabled() const;

      void SetWireFrame(bool arg);
      bool GetWireFrame() const;

      void CreateDeviceDependentResources();
      void ReleaseDeviceDependentResources();

      void Reset();

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>                                  m_deviceResources;
      Physics::PhysicsAPI&                                                  m_physicsAPI;

      // Direct3D resources for SR mesh rendering pipeline.
      Microsoft::WRL::ComPtr<ID3D11InputLayout>                             m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>                            m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>                          m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>                             m_lightingPixelShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>                             m_colorPixelShader;

      // Control variables
      std::atomic_bool                                                      m_renderEnabled = false;

      bool                                                                  m_usingVprtShaders = false;

      Microsoft::WRL::ComPtr<ID3D11RasterizerState>                         m_defaultRasterizerState;
      Microsoft::WRL::ComPtr<ID3D11RasterizerState>                         m_wireframeRasterizerState;

      std::atomic_bool                                                      m_drawWireframe = true;
    };
  }
}