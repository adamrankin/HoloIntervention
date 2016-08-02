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
#include "DirectXHelper.h"
#include "SliceRenderer.h"

// STD includes
#include <sstream>

using namespace DirectX;

namespace TrackedUltrasound
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    SliceRenderer::SliceRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      CreateDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    SliceRenderer::~SliceRenderer()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    uint32 SliceRenderer::AddSlice( byte* imageData, uint32 width, uint32 height )
    {
      SliceEntry entry(width, height);
      entry.m_id = m_nextUnusedSliceId;
      entry.SetImageData(imageData);
      entry.m_constantBuffer.worldMatrix = SimpleMath::Matrix::Identity;
      entry.m_showing = true;
      entry.m_desiredPose = entry.m_currentPose = entry.m_lastPose = SimpleMath::Matrix::Identity;

      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      m_slices.push_back(entry);

      m_nextUnusedSliceId++;
      return m_nextUnusedSliceId - 1;
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::RemoveSlice( uint32 sliceId )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if (FindSlice(sliceId, slice))
      {
        slice->ReleaseDeviceDependentResources();
        for (auto sliceIter = m_slices.begin(); sliceIter != m_slices.end(); ++sliceIter)
        {
          if (sliceIter->m_id == sliceId)
          {
            m_slices.erase(sliceIter);
            return;
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::ShowSlice( uint32 sliceId )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if (FindSlice(sliceId, slice))
      {
        slice->m_showing = true;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::HideSlice( uint32 sliceId )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if (FindSlice(sliceId, slice))
      {
        slice->m_showing = false;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetSliceVisible( uint32 sliceId, bool show )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if ( FindSlice( sliceId, slice ) )
      {
        slice->m_showing = show;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetSlicePose( uint32 sliceId, const XMFLOAT4X4& pose )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if ( FindSlice( sliceId, slice ) )
      {
        slice->m_currentPose = slice->m_desiredPose = slice->m_lastPose = pose;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::SetDesiredSlicePose( uint32 sliceId, const XMFLOAT4X4& pose )
    {
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );
      SliceEntry* slice;
      if ( FindSlice( sliceId, slice ) )
      {
        slice->m_desiredPose = pose;
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::CreateDeviceDependentResources()
    {
      // Lock the slices so we create resources for exactly what we have currently
      std::lock_guard<std::mutex> guard( m_sliceMapMutex );

      m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

      // Load shaders asynchronously.
      task<std::vector<byte>> loadVSTask = DX::ReadDataAsync(m_usingVprtShaders ? L"ms-appx:///SliceVprtVertexShader.cso" : L"ms-appx:///SliceVertexShader.cso");
      task<std::vector<byte>> loadPSTask = DX::ReadDataAsync(L"ms-appx:///SlicePixelShader.cso");

      task<std::vector<byte>> loadGSTask;
      if (!m_usingVprtShaders)
      {
        // Load the pass-through geometry shader.
        // position, color, texture, index
        loadGSTask = DX::ReadDataAsync(L"ms-appx:///PTIGeometryShader.cso");
      }

      // After the vertex shader file is loaded, create the shader and input layout.
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
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
        constexpr std::array<unsigned short, 12> quadIndices =
        {
          {
            // -z
            0, 2, 3,
            0, 1, 2,

            // +z
            2, 0, 3,
            1, 0, 2,
          }
        };

        m_indexCount = quadIndices.size();

        D3D11_SUBRESOURCE_DATA indexBufferData = { 0 };
        indexBufferData.pSysMem = quadIndices.data();
        indexBufferData.SysMemPitch = 0;
        indexBufferData.SysMemSlicePitch = 0;
        const CD3D11_BUFFER_DESC indexBufferDesc(sizeof(unsigned short) * quadIndices.size(), D3D11_BIND_INDEX_BUFFER);
        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateBuffer(
            &indexBufferDesc,
            &indexBufferData,
            &m_indexBuffer
          )
        );

        D3D11_SAMPLER_DESC desc;
        ZeroMemory(&desc, sizeof(D3D11_SAMPLER_DESC));
        desc.Filter = D3D11_FILTER_ANISOTROPIC;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 3;
        desc.MinLOD = 0;
        desc.MaxLOD = 3;
        desc.MipLODBias = 0.f;
        desc.BorderColor[0] = 0.f;
        desc.BorderColor[1] = 0.f;
        desc.BorderColor[2] = 0.f;
        desc.BorderColor[3] = 0.f;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        DX::ThrowIfFailed(
          m_deviceResources->GetD3DDevice()->CreateSamplerState(
            &desc,
            &m_quadTextureSamplerState
          )
        );

        for (auto slice : m_slices)
        {
          slice.CreateDeviceDependentResources();
        }

        // After the assets are loaded, the quad is ready to be rendered.
        m_loadingComplete = true;
      });
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::ReleaseDeviceDependentResources()
    {
      m_inputLayout.Reset();
      m_indexBuffer.Reset();
      m_vertexShader.Reset();
      m_geometryShader.Reset();
      m_pixelShader.Reset();

      for (auto slice : m_slices)
      {
        slice.ReleaseDeviceDependentResources();
      }
    }

    //----------------------------------------------------------------------------
    void SliceRenderer::Update( const DX::StepTimer& timer )
    {

    }

    //----------------------------------------------------------------------------
    void SliceRenderer::Render()
    {
      if ( !m_loadingComplete )
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      std::lock_guard<std::mutex> guard( m_sliceMapMutex );

      context->IASetIndexBuffer(
        m_indexBuffer.Get(),
        DXGI_FORMAT_R16_UINT, // Each index is one 16-bit unsigned integer (short).
        0
      );
      context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      context->IASetInputLayout(m_inputLayout.Get());

      // Attach the vertex shader.
      context->VSSetShader(
        m_vertexShader.Get(),
        nullptr,
        0
      );

      if (!m_usingVprtShaders)
      {
        context->GSSetShader(
          m_geometryShader.Get(),
          nullptr,
          0
        );
      }

      // Attach the pixel shader.
      context->PSSetShader(
        m_pixelShader.Get(),
        nullptr,
        0
      );
      context->PSSetSamplers(
        0,
        1,
        m_quadTextureSamplerState.GetAddressOf()
      );

      for ( auto sliceEntry : m_slices )
      {
        sliceEntry.Render(m_indexCount);
      }
    }

    //----------------------------------------------------------------------------
    bool SliceRenderer::FindSlice( uint32 sliceId, SliceEntry*& sliceEntry )
    {
      for ( auto slice : m_slices )
      {
        if ( slice.m_id == sliceId )
        {
          sliceEntry = &slice;
          return true;
        }
      }

      return false;
    }
  }
}