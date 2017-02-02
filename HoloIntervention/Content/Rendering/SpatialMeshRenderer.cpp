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
#include "SpatialMeshRenderer.h"

// Common includes
#include "DeviceResources.h"
#include "DirectXHelper.h"
#include "StepTimer.h"

// System includes
#include "NotificationSystem.h"
#include "SurfaceAPI.h"

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
    SpatialMeshRenderer::SpatialMeshRenderer(System::NotificationSystem& notificationSystem, const std::shared_ptr<DX::DeviceResources>& deviceResources)
      : m_deviceResources(deviceResources)
      , m_notificationSystem(notificationSystem)
    {
      m_meshCollection.clear();
      CreateDeviceDependentResources();
    };

    //----------------------------------------------------------------------------
    // Called once per frame, maintains and updates the mesh collection.
    void SpatialMeshRenderer::Update(const DX::StepTimer& timer, SpatialCoordinateSystem^ coordinateSystem)
    {
      if (!m_renderEnabled)
      {
        return;
      }

      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      // Only create a surface observer when you need to - do not create a new one each frame.
      if (m_surfaceObserver == nullptr)
      {
        RequestAccessAsync(coordinateSystem);
      }

      if (m_surfaceAccessAllowed && m_surfaceObserver == nullptr)
      {
        InitObserver(coordinateSystem);
      }

      const float timeElapsed = static_cast<float>(timer.GetTotalSeconds());

      // Update meshes as needed, based on the current coordinate system.
      // Also remove meshes that are inactive for too long.
      for (auto iter = m_meshCollection.begin(); iter != m_meshCollection.end();)
      {
        auto& pair = *iter;
        auto& surfaceMesh = pair.second;

        // Update the surface mesh.
        surfaceMesh.Update(timer, coordinateSystem);

        // Check to see if the mesh has expired.
        float lastActiveTime = surfaceMesh.GetLastActiveTime();
        float inactiveDuration = timeElapsed - lastActiveTime;
        if (inactiveDuration > MAX_INACTIVE_MESH_TIME)
        {
          // Surface mesh is expired.
          m_meshCollection.erase(iter++);
        }
        else
        {
          ++iter;
        }
      }
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::InitObserver(SpatialCoordinateSystem^ coordinateSystem)
    {
      SpatialBoundingBox axisAlignedBoundingBox =
      {
        { 0.f,  0.f, 0.f },
        { 20.f, 20.f, 5.f },
      };
      SpatialBoundingVolume^ bounds = SpatialBoundingVolume::FromBox(coordinateSystem, axisAlignedBoundingBox);

      // If status is Allowed, we can create the surface observer.
      if (m_surfaceObserver == nullptr)
      {
        // First, we'll set up the surface observer to use our preferred data formats.
        // In this example, a "preferred" format is chosen that is compatible with our precompiled shader pipeline.
        m_surfaceMeshOptions = ref new SpatialSurfaceMeshOptions();
        IVectorView<DirectXPixelFormat>^ supportedVertexPositionFormats = m_surfaceMeshOptions->SupportedVertexPositionFormats;
        unsigned int formatIndex = 0;
        if (supportedVertexPositionFormats->IndexOf(DirectXPixelFormat::R16G16B16A16IntNormalized, &formatIndex))
        {
          m_surfaceMeshOptions->VertexPositionFormat = DirectXPixelFormat::R16G16B16A16IntNormalized;
        }
        IVectorView<DirectXPixelFormat>^ supportedVertexNormalFormats = m_surfaceMeshOptions->SupportedVertexNormalFormats;
        if (supportedVertexNormalFormats->IndexOf(DirectXPixelFormat::R8G8B8A8IntNormalized, &formatIndex))
        {
          m_surfaceMeshOptions->VertexNormalFormat = DirectXPixelFormat::R8G8B8A8IntNormalized;
        }

        // If you are using a very high detail setting with spatial mapping, it can be beneficial
        // to use a 32-bit unsigned integer format for indices instead of the default 16-bit.
        // Uncomment the following code to enable 32-bit indices.
        //IVectorView<DirectXPixelFormat>^ supportedTriangleIndexFormats = m_surfaceMeshOptions->SupportedTriangleIndexFormats;
        //if (supportedTriangleIndexFormats->IndexOf(DirectXPixelFormat::R32UInt, &formatIndex))
        //{
        //    m_surfaceMeshOptions->TriangleIndexFormat = DirectXPixelFormat::R32UInt;
        //}

        // Create the observer.
        m_surfaceObserver = ref new SpatialSurfaceObserver();
        if (m_surfaceObserver)
        {
          m_surfaceObserver->SetBoundingVolume(bounds);

          // If the surface observer was successfully created, we can initialize our
          // collection by pulling the current data set.
          auto mapContainingSurfaceCollection = m_surfaceObserver->GetObservedSurfaces();
          for (auto const& pair : mapContainingSurfaceCollection)
          {
            // Store the ID and metadata for each surface.
            auto const& id = pair->Key;
            auto const& surfaceInfo = pair->Value;
            AddSurface(id, surfaceInfo);
          }

          // We then subscribe to an event to receive up-to-date data.
          m_surfacesChangedToken = m_surfaceObserver->ObservedSurfacesChanged +=
                                     ref new TypedEventHandler<SpatialSurfaceObserver^, Platform::Object^>(
                                       std::bind(&SpatialMeshRenderer::OnSurfacesChanged, this, std::placeholders::_1, std::placeholders::_2)
                                     );
        }
      }
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::OnSurfacesChanged(SpatialSurfaceObserver^ sender, Object^ args)
    {
      IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection = sender->GetObservedSurfaces();

      // Process surface adds and updates.
      for (const auto& pair : surfaceCollection)
      {
        auto id = pair->Key;
        auto surfaceInfo = pair->Value;

        if (HasSurface(id))
        {
          if (GetLastUpdateTime(id).UniversalTime < surfaceInfo->UpdateTime.UniversalTime)
          {
            // Update existing surface.
            UpdateSurface(id, surfaceInfo);
          }
        }
        else
        {
          // New surface.
          AddSurface(id, surfaceInfo);
        }
      }

      HideInactiveMeshes(surfaceCollection);
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::RequestAccessAsync(SpatialCoordinateSystem^ coordinateSystem)
    {
      // Initialize the Surface Observer using a valid coordinate system.
      if (!m_spatialPerceptionAccessRequested)
      {
        auto initSurfaceObserverTask = create_task(SpatialSurfaceObserver::RequestAccessAsync());
        initSurfaceObserverTask.then([this, coordinateSystem](Windows::Perception::Spatial::SpatialPerceptionAccessStatus status)
        {
          switch (status)
          {
          case SpatialPerceptionAccessStatus::Allowed:
            m_surfaceAccessAllowed = true;
            break;
          default:
            m_surfaceAccessAllowed = false;
            break;
          }
        });

        m_spatialPerceptionAccessRequested = true;
      }
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::AddSurface(Guid id, SpatialSurfaceInfo^ newSurface)
    {
      auto fadeInMeshTask = AddOrUpdateSurfaceAsync(id, newSurface).then([this, id]()
      {
        if (HasSurface(id))
        {
          std::lock_guard<std::mutex> guard(m_meshCollectionLock);

          // In this example, new surfaces are treated differently by highlighting them in a different
          // color. This allows you to observe changes in the spatial map that are due to new meshes,
          // as opposed to mesh updates.
          auto& surfaceMesh = m_meshCollection[id];
          surfaceMesh.SetColorFadeTimer(SURFACE_MESH_FADE_IN_TIME);
        }
      });
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::UpdateSurface(Guid id, SpatialSurfaceInfo^ newSurface)
    {
      AddOrUpdateSurfaceAsync(id, newSurface);
    }

    //----------------------------------------------------------------------------
    Concurrency::task<void> SpatialMeshRenderer::AddOrUpdateSurfaceAsync(Guid id, SpatialSurfaceInfo^ newSurface)
    {
      auto options = ref new SpatialSurfaceMeshOptions();
      options->IncludeVertexNormals = true;

      // The level of detail setting is used to limit mesh complexity, by limiting the number
      // of triangles per cubic meter.
      auto createMeshTask = create_task(newSurface->TryComputeLatestMeshAsync(m_maxTrianglesPerCubicMeter, options));
      auto processMeshTask = createMeshTask.then([this, id](SpatialSurfaceMesh ^ mesh)
      {
        if (mesh != nullptr)
        {
          std::lock_guard<std::mutex> guard(m_meshCollectionLock);

          auto& surfaceMesh = m_meshCollection[id];
          surfaceMesh.SetDeviceResources(m_deviceResources);
          surfaceMesh.UpdateSurface(mesh);
          surfaceMesh.SetIsActive(true);
        }
      }, task_continuation_context::use_current());

      return processMeshTask;
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::RemoveSurface(Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      m_meshCollection.erase(id);
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::ClearSurfaces()
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      m_meshCollection.clear();
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::HideInactiveMeshes(IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      // Hide surfaces that aren't actively listed in the surface collection.
      for (auto& pair : m_meshCollection)
      {
        const auto& id = pair.first;
        auto& surfaceMesh = pair.second;

        surfaceMesh.SetIsActive(surfaceCollection->HasKey(id) ? true : false);
      };
    }

    //----------------------------------------------------------------------------
    // Renders one frame using the vertex, geometry, and pixel shaders.
    void SpatialMeshRenderer::Render()
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

      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      for (auto& pair : m_meshCollection)
      {
        pair.second.Render(m_usingVprtShaders);
      }

      context->RSSetState(nullptr);
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::SetEnabled(bool arg)
    {
      m_renderEnabled = arg;
    }

    //----------------------------------------------------------------------------
    bool SpatialMeshRenderer::GetEnabled() const
    {
      return m_renderEnabled;
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::CreateDeviceDependentResources()
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

        static const D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
        {
          { "POSITION", 0, DXGI_FORMAT_R16G16B16A16_SNORM, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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
        // Recreate device-based surface mesh resources.
        std::lock_guard<std::mutex> guard(m_meshCollectionLock);
        for (auto& iter : m_meshCollection)
        {
          iter.second.ReleaseDeviceDependentResources();
          iter.second.CreateDeviceDependentResources();
        }

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
    void SpatialMeshRenderer::ReleaseDeviceDependentResources()
    {
      m_componentReady = false;

      m_vertexShader.Reset();
      m_inputLayout.Reset();
      m_geometryShader.Reset();
      m_lightingPixelShader.Reset();
      m_colorPixelShader.Reset();

      m_defaultRasterizerState.Reset();
      m_wireframeRasterizerState.Reset();

      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      for (auto& iter : m_meshCollection)
      {
        iter.second.ReleaseDeviceDependentResources();
      }
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::Reset()
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      ReleaseDeviceDependentResources();

      ClearSurfaces();
      m_spatialPerceptionAccessRequested = false;
      m_surfaceAccessAllowed = false;
      m_surfaceMeshOptions = nullptr;
      m_surfaceObserver = nullptr;
      m_drawWireframe = true;
    }

    //----------------------------------------------------------------------------
    void SpatialMeshRenderer::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap)
    {
      callbackMap[L"mesh on"] = [this](SpeechRecognitionResult ^ result)
      {
        m_notificationSystem.QueueMessage(L"Mesh showing.");
        SetEnabled(true);
      };

      callbackMap[L"mesh off"] = [this](SpeechRecognitionResult ^ result)
      {
        m_notificationSystem.QueueMessage(L"Mesh disabled.");
        SetEnabled(false);
      };
    }

    //----------------------------------------------------------------------------
    bool SpatialMeshRenderer::HasSurface(Platform::Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      return m_meshCollection.find(id) != m_meshCollection.end();
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::DateTime SpatialMeshRenderer::GetLastUpdateTime(Platform::Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      auto& meshIter = m_meshCollection.find(id);
      if (meshIter != m_meshCollection.end())
      {
        auto const& mesh = meshIter->second;
        return mesh.GetLastUpdateTime();
      }
      else
      {
        static const Windows::Foundation::DateTime zero;
        return zero;
      }
    }
  }
}