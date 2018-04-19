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

// Local includes
#include "pch.h"
#include "AppView.h"
#include "MeshRenderer.h"

// Common includes
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"
#include "PhysicsAPI.h"

using namespace Concurrency;
using namespace DX;
using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Media::SpeechRecognition;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace
{
  static const float LOOP_DURATION_MSEC = 2000.0f;
}

namespace HoloIntervention
{
  namespace Rendering
  {

    //----------------------------------------------------------------------------
    void MeshRenderer::RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"mesh on"] = [this](SpeechRecognitionResult ^ result)
      {
        this->SetEnabled(true);
      };

      callbackMap[L"mesh off"] = [this](SpeechRecognitionResult ^ result)
      {
        this->SetEnabled(false);
      };

      callbackMap[L"mesh solid"] = [this](SpeechRecognitionResult ^ result)
      {
        this->SetWireFrame(false);
        this->SetEnabled(true);
      };

      callbackMap[L"mesh wireframe"] = [this](SpeechRecognitionResult ^ result)
      {
        this->SetWireFrame(true);
        this->SetEnabled(true);
      };
    }

    //----------------------------------------------------------------------------
    MeshRenderer::MeshRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources, Physics::PhysicsAPI& physics)
      : m_deviceResources(deviceResources)
      , m_physicsAPI(physics)
    {
      CreateDeviceDependentResources();
    };

    //----------------------------------------------------------------------------
    // Renders one frame using the vertex, geometry, and pixel shaders.
    void MeshRenderer::Render()
    {
      // Loading is asynchronous. Only draw geometry after it's loaded.
      if (!m_componentReady || !m_renderEnabled)
      {
        return;
      }

      auto context = m_deviceResources->GetD3DDeviceContext();

      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      // Attach our vertex shader.
      context->VSSetShader(m_vertexShader.Get(), nullptr, 0);

      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_geometryShader.Get(), nullptr, 0);
      }

      if (m_drawWireframe)
      {
        // Use a wireframe rasterizer state.
        context->RSSetState(m_wireframeRasterizerState.Get());

        // Attach a pixel shader to render a solid color wireframe.
        context->PSSetShader(m_colorPixelShader.Get(), nullptr, 0);
      }
      else
      {
        // Use the default rasterizer state.
        context->RSSetState(m_defaultRasterizerState.Get());

        // Attach a pixel shader that can do lighting.
        context->PSSetShader(m_lightingPixelShader.Get(), nullptr, 0);
      }

      auto meshes = m_physicsAPI.GetMeshes();
      for (auto& pair : meshes)
      {
        pair.second->Render(m_usingVprtShaders);
      }

      context->RSSetState(nullptr);
    }

    //----------------------------------------------------------------------------
    void MeshRenderer::SetEnabled(bool arg)
    {
      m_renderEnabled = arg;
    }

    //----------------------------------------------------------------------------
    bool MeshRenderer::GetEnabled() const
    {
      return m_renderEnabled;
    }

    //----------------------------------------------------------------------------
    void MeshRenderer::SetWireFrame(bool arg)
    {
      m_drawWireframe = arg;
    }

    //----------------------------------------------------------------------------
    bool MeshRenderer::GetWireFrame() const
    {
      return m_drawWireframe;
    }

    //----------------------------------------------------------------------------
    void MeshRenderer::CreateDeviceDependentResources()
    {
      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      std::wstring vertexShaderFileName = m_usingVprtShaders ? L"ms-appx:///SMRSurfaceVprtVertexShader.cso" : L"ms-appx:///SMRSurfaceVertexShader.cso";

      // Load shaders asynchronously.
      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(vertexShaderFileName);
      task<std::vector<byte>> loadLightingPSTask = DX::ReadDataAsync(L"ms-appx:///SMRLightingPixelShader.cso");
      task<std::vector<byte>> loadWireframePSTask = DX::ReadDataAsync(L"ms-appx:///SMRSolidColorPixelShader.cso");

      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        // Load the pass-through geometry shader.
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PPNCIGeometryShader.cso");
      }

      // After the vertex shader file is loaded, create the shader and input layout.
      auto createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateVertexShader(&fileData[0], fileData.size(), nullptr, &m_vertexShader)
        );

        while (m_physicsAPI.GetMeshOptions() == nullptr)
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        auto positionFormat = (m_physicsAPI.GetMeshOptions()->VertexPositionFormat == DirectXPixelFormat::R32G32B32A32Float) ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
        static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
        {
          { "POSITION", 0, positionFormat,                 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          { "NORMAL",   0, DXGI_FORMAT_R8G8B8A8_SNORM,     1, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateInputLayout(vertexDesc, ARRAYSIZE(vertexDesc), &fileData[0], fileData.size(), &m_inputLayout)
        );
      });

      // After the pixel shader file is loaded, create the shader and constant buffer.
      auto createLightingPSTask = loadLightingPSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreatePixelShader(&fileData[0], fileData.size(), nullptr, &m_lightingPixelShader)
        );
      });

      // After the pixel shader file is loaded, create the shader and constant buffer.
      auto createWireframePSTask = loadWireframePSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreatePixelShader(&fileData[0], fileData.size(), nullptr, &m_colorPixelShader)
        );
      });

      task<void> createGSTask;
      if (!m_usingVprtShaders)
      {
        // After the pass-through geometry shader file is loaded, create the shader.
        createGSTask = loadGSTask.then([this](const std::vector<byte>& fileData)
        {
          DX::ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateGeometryShader(&fileData[0], fileData.size(), nullptr, &m_geometryShader)
          );
        });
      }

      // Once all shaders are loaded, create the mesh.
      task<void> shaderTaskGroup = m_usingVprtShaders ?
                                   (createLightingPSTask && createWireframePSTask && createVSTask) :
                                   (createLightingPSTask && createWireframePSTask && createVSTask && createGSTask);

      // Once the cube is loaded, the object is ready to be rendered.
      auto finishLoadingTask = shaderTaskGroup.then([this]()
      {
        // Create a default rasterizer state descriptor.
        D3D11_RASTERIZER_DESC rasterizerDesc = CD3D11_RASTERIZER_DESC(D3D11_DEFAULT);

        // Create the default rasterizer state.
        m_deviceResources->GetD3DDevice()->CreateRasterizerState(&rasterizerDesc, m_defaultRasterizerState.GetAddressOf());

        // Change settings for wireframe rasterization.
        rasterizerDesc.AntialiasedLineEnable = true;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;

        // Create a wireframe rasterizer state.
        m_deviceResources->GetD3DDevice()->CreateRasterizerState(&rasterizerDesc, m_wireframeRasterizerState.GetAddressOf());

        m_componentReady = true;
      });
    }

    //----------------------------------------------------------------------------
    void MeshRenderer::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;

      m_vertexShader.Reset();
      m_inputLayout.Reset();
      m_geometryShader.Reset();
      m_lightingPixelShader.Reset();
      m_colorPixelShader.Reset();

      m_defaultRasterizerState.Reset();
      m_wireframeRasterizerState.Reset();
    }

    //----------------------------------------------------------------------------
    void MeshRenderer::Reset()
    {
      ReleaseDeviceDependentResources();
      m_drawWireframe = true;
    }
  }
}