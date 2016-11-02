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
#include "VolumeRenderer.h"

// Common includes
#include "DeviceResources.h"
#include "StepTimer.h"
#include "DirectXHelper.h"

using namespace Concurrency;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    VolumeRenderer::VolumeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
    {
    }

    //----------------------------------------------------------------------------
    VolumeRenderer::~VolumeRenderer()
    {
      delete m_transferFunction;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Update(SpatialPointerPose^ pose, const DX::StepTimer& timer)
    {

    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::Render()
    {
      if (!m_loadingComplete)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      const UINT stride = sizeof(VertexPosition);
      const UINT offset = 0;
      context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
      context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());
      context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
      context->VSSetConstantBuffers(0, 1, m_modelConstantBuffer.GetAddressOf());
      if (!m_usingVprtShaders)
      {
        context->GSSetShader(m_geometryShader.Get(), nullptr, 0);
      }
      context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
      context->DrawIndexedInstanced(m_indexCount, 2, 0, 0, 0);
    }

    //----------------------------------------------------------------------------
    task<void> VolumeRenderer::SetTransferFunctionTypeAsync(TransferFunctionType type)
    {
      return create_task([this, type]()
      {
        std::lock_guard<std::mutex> guard(m_tfMutex);

        delete m_transferFunction;
        switch (type)
        {
        case VolumeRenderer::TransferFunction_Piecewise_Linear:
        default:
          m_tfType = VolumeRenderer::TransferFunction_Piecewise_Linear;
          m_transferFunction = new PiecewiseLinearTF();
          break;
        }

        m_transferFunction->Update();
      }).then([this]()
      {
        ReleaseDeviceDependentResources();
        return CreateDeviceDependentResourcesAsync();
      });
    }

    //----------------------------------------------------------------------------
    task<void> VolumeRenderer::CreateDeviceDependentResourcesAsync()
    {
      // load shader code, compile depending on settings requested
      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      std::wstring vertexShaderFileName = m_usingVprtShaders
                                          ? L"ms-appx:///VolumeRendererVprtVertexShader.cso"
                                          : L"ms-appx:///VolumeRendererVertexShader.cso";

      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(vertexShaderFileName);
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///VolumeRendererPixelShader.cso");
      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        // Load the pass-through geometry shader.
        // position, index
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PIGeometryShader.cso");
      }

      {
        // set up transfer function gpu memory
        std::lock_guard<std::mutex> guard(m_tfMutex);
      }

      task<void> createVSTask = loadVSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateVertexShader(
            fileData.data(),
            fileData.size(),
            nullptr,
            &m_vertexShader
          )
        );

        constexpr std::array<D3D11_INPUT_ELEMENT_DESC, 3> vertexDesc =
        {
          {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
          }
        };

        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateInputLayout(
            vertexDesc.data(),
            vertexDesc.size(),
            fileData.data(),
            fileData.size(),
            &m_inputLayout
          )
        );
      });

      // After the pixel shader file is loaded, create the shader and constant buffer.
      task<void> createPSTask = loadPSTask.then([this](const std::vector<byte>& fileData)
      {
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreatePixelShader(
            fileData.data(),
            fileData.size(),
            nullptr,
            &m_pixelShader
          )
        );

        const CD3D11_BUFFER_DESC constantBufferDesc(sizeof(VolumeConstantBuffer), D3D11_BIND_CONSTANT_BUFFER);
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBuffer(
            &constantBufferDesc,
            nullptr,
            &m_modelConstantBuffer
          )
        );
      });

      task<void> createGSTask;
      if (!m_usingVprtShaders)
      {
        // After the geometry shader file is loaded, create the shader.
        createGSTask = loadGSTask.then([this](const std::vector<byte>& fileData)
        {
          DX::ThrowIfFailed(
            m_deviceResources->GetD3DDevice()->CreateGeometryShader(
              fileData.data(),
              fileData.size(),
              nullptr,
              &m_geometryShader
            )
          );
        });
      }

      // Once all shaders are loaded, create the mesh.
      task<void> shaderTaskGroup = m_usingVprtShaders ? (createPSTask && createVSTask) : (createPSTask && createVSTask && createGSTask);
      task<void> finishLoadingTask = shaderTaskGroup.then([this]()
      {
        CreateVertexResources();
      });

      return finishLoadingTask;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseDeviceDependentResources()
    {
      {
        std::lock_guard<std::mutex> guard(m_tfMutex);
        delete m_transferFunction;
        m_transferFunction = nullptr;
      }

      ReleaseVertexResources();

      m_vertexShader.Reset();
      m_inputLayout.Reset();
      m_pixelShader.Reset();
      m_geometryShader.Reset();
      m_modelConstantBuffer.Reset();
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::CreateVertexResources()
    {
      static const std::array<VertexPosition, 8> cubeVertices =
      {
        {
          { float3(-0.5f, -0.5f, -0.5f), },
          { float3(-0.5f, -0.5f,  0.5f), },
          { float3(-0.5f,  0.5f, -0.5f), },
          { float3(-0.5f,  0.5f,  0.5f), },
          { float3(0.5f, -0.5f, -0.5f), },
          { float3(0.5f, -0.5f,  0.5f), },
          { float3(0.5f,  0.5f, -0.5f), },
          { float3(0.5f,  0.5f,  0.5f), },
        }
      };

      D3D11_SUBRESOURCE_DATA vertexBufferData = { 0 };
      vertexBufferData.pSysMem = cubeVertices.data();
      vertexBufferData.SysMemPitch = 0;
      vertexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC vertexBufferDesc(sizeof(cubeVertices), D3D11_BIND_VERTEX_BUFFER);
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &vertexBufferDesc,
          &vertexBufferData,
          &m_vertexBuffer
        )
      );

      constexpr std::array<uint16_t, 36> cubeIndices =
      {
        {
          2, 1, 0, // -x
          2, 3, 1,

          6, 4, 5, // +x
          6, 5, 7,

          0, 1, 5, // -y
          0, 5, 4,

          2, 6, 7, // +y
          2, 7, 3,

          0, 4, 6, // -z
          0, 6, 2,

          1, 3, 7, // +z
          1, 7, 5,
        }
      };

      m_indexCount = cubeIndices.size();

      D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
      indexBufferData.pSysMem = cubeIndices.data();
      indexBufferData.SysMemPitch = 0;
      indexBufferData.SysMemSlicePitch = 0;
      const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(cubeIndices), D3D11_BIND_INDEX_BUFFER);
      DX::ThrowIfFailed(
        m_deviceResources->GetD3DDevice()->CreateBuffer(
          &indexBufferDesc,
          &indexBufferData,
          &m_indexBuffer
        )
      );

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void VolumeRenderer::ReleaseVertexResources()
    {
      m_loadingComplete = false;

      m_indexBuffer.Reset();
      m_vertexBuffer.Reset();
    }
  }
}