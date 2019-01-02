/*====================================================================
Copyright(c) 2018 Adam Rankin


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
#include "Common\Common.h"
#include "SpatialSurfaceCollection.h"

// Common includes
#include "Rendering\DeviceResources.h"
#include "Rendering\DirectXHelper.h"
#include "Common\StepTimer.h"

// Windows includes
#include <comdef.h>
#include <ppl.h>

// STL includes
#include <sstream>

using namespace Concurrency;
using namespace DirectX;
using namespace DX;
using namespace Platform;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Graphics::DirectX;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Perception::Spatial;

namespace
{
  inline float magnitude(const float3& vector)
  {
    return sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
  }
}

namespace Valhalla
{
  namespace Spatial
  {
    const float SpatialSurfaceCollection::MAX_INACTIVE_MESH_TIME_SEC = 120.f;
    const uint64_t SpatialSurfaceCollection::FRAMES_BEFORE_EXPIRED = 2;
    const float SpatialSurfaceCollection::SURFACE_MESH_FADE_IN_TIME = 3.0f;

    //----------------------------------------------------------------------------
    SpatialSurfaceCollection::SpatialSurfaceCollection(const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer)
      : m_deviceResources(deviceResources)
      , m_stepTimer(stepTimer)
    {
      try
      {
        CreateDeviceDependentResourcesAsync();
      }
      catch(const std::exception& e)
      {
        LOG_ERROR(std::string("Cannot create device resources: ") + e.what());
      }
    };

    //----------------------------------------------------------------------------
    SpatialSurfaceCollection::~SpatialSurfaceCollection()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    // Called once per frame, maintains and updates the mesh collection.
    void SpatialSurfaceCollection::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      const float timeElapsed = static_cast<float>(m_stepTimer.GetTotalSeconds());

      // Update meshes as needed, based on the current coordinate system.
      // Also remove meshes that are inactive for too long.
      for(auto iter = m_meshCollection.begin(); iter != m_meshCollection.end();)
      {
        auto& pair = *iter;
        auto& surfaceMesh = pair.second;

        // Update the surface mesh.
        surfaceMesh->Update(m_stepTimer, coordinateSystem);

        // Check to see if the mesh has expired.
        float lastActiveTime = surfaceMesh->GetLastActiveTime();
        float inactiveDuration = timeElapsed - lastActiveTime;

        if(inactiveDuration > MAX_INACTIVE_MESH_TIME_SEC)
        {
          // Surface mesh is expired.
          iter = m_meshCollection.erase(iter);
        }
        else
        {
          ++iter;
        }
      };
    }

    //----------------------------------------------------------------------------
    task<void> SpatialSurfaceCollection::CreateDeviceDependentResourcesAsync()
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      if(m_resourcesLoaded)
      {
        return create_task([]() {});
      }

      for(auto pair : m_meshCollection)
      {
        pair.second->CreateDeviceDependentResources();
      }

      D3D11_BUFFER_DESC constant_buffer_desc;
      ZeroMemory(&constant_buffer_desc, sizeof(constant_buffer_desc));
      constant_buffer_desc.ByteWidth = sizeof(RayConstantBuffer);
      constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
      constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
      constant_buffer_desc.CPUAccessFlags = 0;

      DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateBuffer(&constant_buffer_desc, nullptr, &m_constantBuffer));
#if _DEBUG
      m_constantBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, sizeof("SpatSurfCollConstBuffer") - 1, "SpatSurfCollConstBuffer");
#endif

      return DX::ReadDataAsync(L"ms-appx:///CSRayTriangleIntersection.cso").then([ = ](std::vector<byte> data)
      {
        DX::ThrowIfFailed(m_deviceResources->GetD3DDevice()->CreateComputeShader(&data.front(), data.size(), nullptr, &m_d3d11ComputeShader));

#if defined(_DEBUG) || defined(PROFILE)
        m_d3d11ComputeShader->SetPrivateData(WKPDID_D3DDebugObjectName, strlen("RayTriangleIntersectionCS") - 1, "RayTriangleIntersectionCS");
#endif
        m_resourcesLoaded = true;
      });
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::ReleaseDeviceDependentResources()
    {
      m_resourcesLoaded = false;
      m_d3d11ComputeShader = nullptr;
      m_constantBuffer = nullptr;

      for(auto pair : m_meshCollection)
      {
        pair.second->ReleaseDeviceDependentResources();
      }
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::AddSurface(Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions)
    {
      auto fadeInMeshTask = AddOrUpdateSurfaceAsync(id, newSurface, meshOptions).then([this, id]()
      {
        if(HasSurface(id))
        {
          std::lock_guard<std::mutex> guard(m_meshCollectionLock);

          // In this example, new surfaces are treated differently by highlighting them in a different
          // color. This allows you to observe changes in the spatial map that are due to new meshes,
          // as opposed to mesh updates.
          auto& surfaceMesh = m_meshCollection[id];
          surfaceMesh->SetColorFadeTimer(SURFACE_MESH_FADE_IN_TIME);
        }
      });
    }

    //----------------------------------------------------------------------------
    task<void> SpatialSurfaceCollection::AddOrUpdateSurfaceAsync(Guid id, SpatialSurfaceInfo^ newSurface, SpatialSurfaceMeshOptions^ meshOptions)
    {
      // The level of detail setting is used to limit mesh complexity, by limiting the number
      // of triangles per cubic meter.
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      auto createMeshTask = create_task(newSurface->TryComputeLatestMeshAsync(m_maxTrianglesPerCubicMeter, meshOptions));
      auto processMeshTask = createMeshTask.then([this, id, newSurface, meshOptions](SpatialSurfaceMesh ^ mesh)
      {
        if(mesh != nullptr)
        {
          auto normals = mesh->VertexNormals;

          std::lock_guard<std::mutex> guard(m_meshCollectionLock);

          auto entry = m_meshCollection.find(id);

          if(entry == m_meshCollection.end())
          {
            m_meshCollection[id] = std::make_shared<SurfaceMesh>(m_deviceResources);
          }

          auto& surfaceMesh = m_meshCollection[id];
          surfaceMesh->UpdateSurface(mesh);
          surfaceMesh->SetIsActive(true);
        }
      }, task_continuation_context::use_current());

      return processMeshTask;
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::RemoveSurface(Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      m_meshCollection.erase(id);
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::ClearSurfaces()
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      m_meshCollection.clear();
    }

    //----------------------------------------------------------------------------
    Valhalla::Spatial::SpatialSurfaceCollection::GuidMeshMap SpatialSurfaceCollection::GetSurfaces() const
    {
      return m_meshCollection;
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::TestRayIntersection(SpatialCoordinateSystem^ desiredCoordinateSystem, const float3 rayOrigin, const float3 rayDirection, float3& outHitPosition, float3& outHitNormal, float3& outHitEdge)
    {
      struct hitResult
      {
        hitResult(std::shared_ptr<SurfaceMesh> mesh, Platform::Guid guid): hitMesh(mesh), hitGuid(guid) {}

        float3                        hitPosition;
        float3                        hitNormal;
        float3                        hitEdge;
        std::shared_ptr<SurfaceMesh>  hitMesh;
        Platform::Guid                hitGuid;
      };

      if(!m_resourcesLoaded)
      {
        return false;
      }

      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      uint64 currentFrame = m_stepTimer.GetFrameCount();

      // Perform CPU based pre-check using OBB
      std::mutex potentialHitsMutex;
      GuidMeshMap potentialHits;
      parallel_for_each(m_meshCollection.begin(), m_meshCollection.end(), [this, currentFrame, desiredCoordinateSystem, rayOrigin, rayDirection, &potentialHits, &potentialHitsMutex](auto pair)
      {
        auto mesh = pair.second;

        if(mesh->TestRayOBBIntersection(desiredCoordinateSystem, currentFrame, rayOrigin, rayDirection))
        {
          std::lock_guard<std::mutex> lock(potentialHitsMutex);
          potentialHits[pair.first] = pair.second;
        }
      });

      bool collisionFound(false);

      if(potentialHits.size() > 0)
      {
        LOG_DEBUG(L"potentialHits size: " + potentialHits.size().ToString());

        m_deviceResources->GetD3DDeviceContext()->CSSetShader(m_d3d11ComputeShader.Get(), nullptr, 0);

        RayConstantBuffer buffer;
        buffer.rayOrigin = XMFLOAT4(rayOrigin.x, rayOrigin.y, rayOrigin.z, 1.f);
        XMStoreFloat4(&buffer.rayDirection, XMVector4Normalize(XMLoadFloat4(&XMFLOAT4(rayDirection.x, rayDirection.y, rayDirection.z, 1.f))));
        m_deviceResources->GetD3DDeviceContext()->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &buffer, 0, 0);
        m_deviceResources->GetD3DDeviceContext()->CSSetConstantBuffers(1, 1, m_constantBuffer.GetAddressOf());

        // Check all potential hits
        std::vector<hitResult> results;

        for(auto& pair : m_meshCollection)
        {
          hitResult hitRes(pair.second, pair.first);

          if(pair.second->TestRayIntersection(*m_deviceResources->GetD3DDeviceContext(), currentFrame, hitRes.hitPosition, hitRes.hitNormal, hitRes.hitEdge))
          {
            results.push_back(hitRes);
          }
        }

        // find the closest hit, and submit that as our accepted hit
        LOG_DEBUG(L"results size: " + results.size().ToString());

        if(results.size() > 0)
        {
          int i = 0;
          int closestIndex = 0;
          float3 closestPosition = results[0].hitPosition;

          for(auto& result : results)
          {
            LOG_DEBUG(L"hitposition: " + result.hitPosition.x.ToString() + L", " + result.hitPosition.y.ToString() + L", " + result.hitPosition.z.ToString());
            LOG_DEBUG(L"hitmagnitude: " + magnitude(result.hitPosition).ToString());
            LOG_DEBUG(L"hitmesh: " + result.hitGuid.ToString());

            if(magnitude(result.hitPosition) < magnitude(closestPosition))
            {

              closestIndex = i;
              closestPosition = result.hitPosition;
            }

            ++i;
          }

          outHitPosition = results[closestIndex].hitPosition;
          outHitNormal = results[closestIndex].hitNormal;
          outHitEdge = results[closestIndex].hitEdge;
          m_lastHitMesh = results[closestIndex].hitMesh;
          m_lastHitMeshGuid = results[closestIndex].hitGuid;
          collisionFound = true;
        }

        ID3D11Buffer* ppCBnullptr[1] = { nullptr };
        m_deviceResources->GetD3DDeviceContext()->CSSetConstantBuffers(1, 1, ppCBnullptr);
        m_deviceResources->GetD3DDeviceContext()->CSSetShader(nullptr, nullptr, 0);
      }

      return collisionFound;
    }

    //----------------------------------------------------------------------------
    void SpatialSurfaceCollection::HideInactiveMeshes(IMapView<Guid, SpatialSurfaceInfo^>^ const& surfaceCollection)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);

      // Hide surfaces that aren't actively listed in the surface collection.
      for(auto& pair : m_meshCollection)
      {
        const auto& id = pair.first;
        auto& surfaceMesh = pair.second;

        surfaceMesh->SetIsActive(surfaceCollection->HasKey(id) ? true : false);
      };
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::GetLastHitPosition(float3& position, bool considerOldHits /* = false */)
    {
      if(m_lastHitMesh != nullptr)
      {
        if(!considerOldHits)
        {
          uint64_t frames = m_stepTimer.GetFrameCount() - m_lastHitMesh->GetLastHitFrameNumber();

          if(frames > FRAMES_BEFORE_EXPIRED)
          {
            return false;
          }
        }

        try
        {
          position = m_lastHitMesh->GetLastHitPosition();
        }
        catch(const std::exception& e)
        {
          LOG(LOG_LEVEL_ERROR, e.what());
          return false;
        }

        return true;
      }

      return false;
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::GetLastHitNormal(float3& normal, bool considerOldHits /*= false*/)
    {
      if(m_lastHitMesh != nullptr)
      {
        if(!considerOldHits)
        {
          uint64_t frames = m_stepTimer.GetFrameCount() - m_lastHitMesh->GetLastHitFrameNumber();

          if(frames > FRAMES_BEFORE_EXPIRED)
          {
            return false;
          }
        }

        try
        {
          normal = m_lastHitMesh->GetLastHitNormal();
        }
        catch(const std::exception& e)
        {
          LOG(LOG_LEVEL_ERROR, e.what());
          return false;
        }

        return true;
      }

      return false;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Valhalla::Spatial::SurfaceMesh> SpatialSurfaceCollection::GetLastHitMesh()
    {
      return m_lastHitMesh;
    }

    //----------------------------------------------------------------------------
    Platform::Guid SpatialSurfaceCollection::GetLastHitMeshGuid()
    {
      return m_lastHitMeshGuid;
    }

    //----------------------------------------------------------------------------
    bool SpatialSurfaceCollection::HasSurface(Platform::Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      return m_meshCollection.find(id) != m_meshCollection.end();
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::DateTime SpatialSurfaceCollection::GetLastUpdateTime(Platform::Guid id)
    {
      std::lock_guard<std::mutex> guard(m_meshCollectionLock);
      auto& meshIter = m_meshCollection.find(id);

      if(meshIter != m_meshCollection.end())
      {
        auto const& mesh = meshIter->second;
        return mesh->GetLastUpdateTime();
      }
      else
      {
        static const Windows::Foundation::DateTime zero;
        return zero;
      }
    }
  }
}