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

// Local includes
#include "pch.h"
#include "Common.h"
#include "CameraResources.h"
#include "DeviceResources.h"
#include "ModelEntry.h"
#include "RenderingCommon.h"
#include "StepTimer.h"

// DirectXTK includes
#include <CommonStates.h>
#include <DirectXHelper.h>
#include <Effects.h>
#include <InstancedEffects.h>
#include <Model.h>

// STL includes
#include <algorithm>

// Unnecessary, but removes intellisense errors
#include "Log.h"
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation, DX::StepTimer& timer)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
    {
      // Validate asset location
      Platform::String^ mainFolderLocation = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;

      auto folderTask = create_task(StorageFolder::GetFolderFromPathAsync(mainFolderLocation)).then([this, assetLocation](task<StorageFolder^> folderTask)
      {
        StorageFolder^ folder(nullptr);
        try
        {
          folder = folderTask.get();
        }
        catch (const std::exception&)
        {
          HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, "Unable to locate installed folder path.");
          m_failedLoad = true;
          return;
        }
        std::string asset(assetLocation.begin(), assetLocation.end());

        char drive[32];
        char dir[32767];
        char name[2048];
        char ext[32];
        _splitpath_s(asset.c_str(), drive, dir, name, ext);

        std::string nameStr(name);
        std::string extStr(ext);
        std::string dirStr(dir);
        std::replace(dirStr.begin(), dirStr.end(), '/', '\\');

        if (extStr.empty())
        {
          extStr = ".cmo";
        }
        if (dirStr.find("Assets\\Models\\") != 0)
        {
          dirStr.insert(0, "Assets\\Models\\");
        }
        std::wstring wDir(dirStr.begin(), dirStr.end());

        m_assetLocation = wDir + std::wstring(begin(nameStr), end(nameStr)) + std::wstring(begin(extStr), end(extStr));

        create_task(folder->GetFolderAsync(ref new Platform::String(wDir.c_str()))).then([this, nameStr, extStr](concurrency::task<StorageFolder^> previousTask)
        {
          StorageFolder^ folder;
          try
          {
            folder = previousTask.get();
          }
          catch (Platform::InvalidArgumentException^ e)
          {
            HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, L"InvalidArgumentException: " + e->Message);
            m_failedLoad = true;
            return;
          }
          catch (const std::exception& e)
          {
            HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to get subfolder: ") + e.what());
            m_failedLoad = true;
            return;
          }
          std::string filename(nameStr);
          filename.append(extStr);
          std::wstring wFilename(filename.begin(), filename.end());

          Concurrency::create_task(folder->GetFileAsync(ref new Platform::String(wFilename.c_str()))).then([ this ](StorageFile ^ file)
          {
            if (file != nullptr)
            {
              try
              {
                CreateDeviceDependentResources();
              }
              catch (const std::exception& e)
              {
                HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load model. ") + e.what());
                m_failedLoad = true;
                return;
              }
            }
          });
        });
      });
    }

    //----------------------------------------------------------------------------
    ModelEntry::~ModelEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Update(const DX::CameraResources* cameraResources)
    {
      m_cameraResources = cameraResources;

      const float deltaTime = static_cast<float>(m_timer.GetElapsedSeconds());

      if (m_enableLerp)
      {
        m_currentPose = lerp(m_currentPose, m_desiredPose, std::fmin(deltaTime * m_poseLerpRate, 1.f));
      }
      else
      {
        m_currentPose = m_desiredPose;
      }
      const float3 deltaPosition = transform(float3(0.f, 0.f, 0.f), m_currentPose - m_lastPose); // meters
      m_velocity = deltaPosition * (1.f / deltaTime); // meters per second
      m_lastPose = m_currentPose;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Render()
    {
      if (!m_loadingComplete || !m_visible)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      // Draw opaque parts
      for (auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it)
      {
        auto mesh = it->get();
        assert(mesh != 0);

        mesh->PrepareForRendering(context, *m_states, false, m_wireframe);

        DrawMesh(*mesh, false);
      }

      // Draw alpha parts
      for (auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it)
      {
        auto mesh = it->get();
        assert(mesh != 0);

        mesh->PrepareForRendering(context, *m_states, true, m_wireframe);

        DrawMesh(*mesh, true);
      }

      // Clean up after rendering
      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
      context->OMSetDepthStencilState(nullptr, 0);
      context->RSSetState(nullptr);
    }

    //----------------------------------------------------------------------------
    void ModelEntry::CreateDeviceDependentResources()
    {
      m_states = std::make_unique<DirectX::CommonStates>(m_deviceResources->GetD3DDevice());
      m_effectFactory = std::make_unique<DirectX::InstancedEffectFactory>(m_deviceResources->GetD3DDevice());
      m_effectFactory->SetSharing(false);   // Disable re-use of effect shaders, as this prevents us from rendering different colours
      m_model = std::shared_ptr<DirectX::Model>(std::move(DirectX::Model::CreateFromCMO(m_deviceResources->GetD3DDevice(), m_assetLocation.c_str(), *m_effectFactory)));
      CalculateBounds();

      // Cache default effect colours
      m_model->UpdateEffects([this](IEffect * effect)
      {
        InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
        if (basicEffect != nullptr)
        {
          XMFLOAT4 temp(0.f, 0.f, 0.f, 1.f);
          XMStoreFloat4(&temp, basicEffect->GetDiffuseColor());
          temp.w = basicEffect->GetAlpha();
          m_defaultColours[effect] = temp;
        }
      });

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::ReleaseDeviceDependentResources()
    {
      m_loadingComplete = false;

      m_modelBounds = { -1.f };
      m_model = nullptr;
      m_effectFactory = nullptr;
      m_states = nullptr;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetVisible(bool enable)
    {
      m_visible = enable;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::ToggleVisible()
    {
      m_visible = !m_visible;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetRenderingState(ModelRenderingState state)
    {
      if (!m_loadingComplete)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, "Attempting to change rendering state before model is loaded.");
        return;
      }

      if (state == RENDERING_GREYSCALE)
      {
        RenderGreyscale();
      }
      else if (state == RENDERING_DEFAULT)
      {
        RenderDefault();
      }
      else
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, "Unknown render state requested.");
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetDesiredPose(const float4x4& world)
    {
      m_desiredPose = world;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetCurrentPose(const Windows::Foundation::Numerics::float4x4& world)
    {
      m_currentPose = m_desiredPose = world;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 ModelEntry::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    float3 ModelEntry::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::EnableLighting(bool enable)
    {
      if (m_model == nullptr)
      {
        return;
      }

      m_model->UpdateEffects([this, enable](IEffect * effect)
      {
        InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
        if (basicEffect != nullptr)
        {
          basicEffect->SetLightingEnabled(enable);
        }
      });
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetCullMode(D3D11_CULL_MODE mode)
    {
      if (mode == D3D11_CULL_FRONT)
      {
        for (auto& mesh : m_model->meshes)
        {
          mesh->ccw = false;
        }
      }
      else if (mode == D3D11_CULL_BACK)
      {
        for (auto& mesh : m_model->meshes)
        {
          mesh->ccw = true;
        }
      }
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::FailedLoad() const
    {
      return m_failedLoad;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetPoseLerpRate(float lerpRate)
    {
      m_poseLerpRate = lerpRate;
    }

    //----------------------------------------------------------------------------
    uint64 ModelEntry::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    std::array<float, 6> ModelEntry::GetBounds() const
    {
      return m_modelBounds;
    }

    //----------------------------------------------------------------------------
    std::wstring ModelEntry::GetAssetLocation() const
    {
      return m_assetLocation;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::GetLerpEnabled() const
    {
      return m_enableLerp;
    }

    //----------------------------------------------------------------------------
    float ModelEntry::GetLerpRate() const
    {
      return m_poseLerpRate;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderGreyscale()
    {
      m_model->UpdateEffects([this](IEffect * effect)
      {
        InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
        if (basicEffect != nullptr)
        {
          basicEffect->SetColorAndAlpha(XMLoadFloat4(&float4(0.8f, 0.8f, 0.8f, 1.0f)));
        }
      });
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderDefault()
    {
      m_model->UpdateEffects([this](IEffect * effect)
      {
        InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
        if (basicEffect != nullptr)
        {
          basicEffect->SetColorAndAlpha(XMLoadFloat4(&m_defaultColours[effect]));
        }
      });
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetWireframe(bool wireframe)
    {
      m_wireframe = wireframe;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsLoaded() const
    {
      return m_loadingComplete;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsInFrustum() const
    {
      // TODO : this is a cached value, so in theory this could produce artifacts, bad enough to fix?
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsInFrustum(const SpatialBoundingFrustum& frustum) const
    {
      if (m_timer.GetFrameCount() == m_frustumCheckFrameNumber)
      {
        return m_isInFrustum;
      }

      // The normals for the 6 planes each face out from the frustum, defining its volume
      std::vector<float3> points
      {
        transform(float3(m_modelBounds[0], m_modelBounds[2], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[1], m_modelBounds[2], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[0], m_modelBounds[2], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[1], m_modelBounds[2], m_modelBounds[5]), m_currentPose),
        transform(float3(m_modelBounds[0], m_modelBounds[3], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[1], m_modelBounds[3], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[0], m_modelBounds[3], m_modelBounds[4]), m_currentPose),
        transform(float3(m_modelBounds[1], m_modelBounds[3], m_modelBounds[5]), m_currentPose)
      };

      m_isInFrustum = HoloIntervention::IsInFrustum(frustum, points);
      m_frustumCheckFrameNumber = m_timer.GetFrameCount();
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::EnablePoseLerp(bool enable)
    {
      m_enableLerp = enable;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::DrawMesh(const DirectX::ModelMesh& mesh, bool alpha, std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState)
    {
      assert(m_deviceResources->GetD3DDeviceContext() != 0);

      for (auto it = mesh.meshParts.cbegin(); it != mesh.meshParts.cend(); ++it)
      {
        auto part = (*it).get();
        assert(part != 0);

        if (part->isAlpha != alpha)
        {
          // Skip alpha parts when drawing opaque or skip opaque parts if drawing alpha
          continue;
        }

        auto imatrices = dynamic_cast<DirectX::IStereoEffectMatrices*>(part->effect.get());
        if (imatrices)
        {
          FXMMATRIX view[2] =
          {
            XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().view[0])),
            XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().view[1]))
          };
          FXMMATRIX projection[2] =
          {
            XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[0])),
            XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[1]))
          };

          imatrices->SetMatrices(DirectX::XMLoadFloat4x4(&m_currentPose), view, projection);
        }

        DrawMeshPart(*part, setCustomState);
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::DrawMeshPart(const DirectX::ModelMeshPart& part, std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState)
    {
      m_deviceResources->GetD3DDeviceContext()->IASetInputLayout(part.inputLayout.Get());

      auto vb = part.vertexBuffer.Get();
      UINT vbStride = part.vertexStride;
      UINT vbOffset = 0;
      m_deviceResources->GetD3DDeviceContext()->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
      m_deviceResources->GetD3DDeviceContext()->IASetIndexBuffer(part.indexBuffer.Get(), part.indexFormat, 0);

      part.effect->Apply(m_deviceResources->GetD3DDeviceContext());

      // Hook lets the caller replace our shaders or state settings with whatever else they see fit.
      if (setCustomState)
      {
        setCustomState(part.effect);
      }

      m_deviceResources->GetD3DDeviceContext()->IASetPrimitiveTopology(part.primitiveType);
      m_deviceResources->GetD3DDeviceContext()->DrawIndexedInstanced(part.indexCount, 2, part.startIndex, part.vertexOffset, 0);
    }

    //----------------------------------------------------------------------------
    void ModelEntry::UpdateEffects(_In_ std::function<void __cdecl(DirectX::IEffect*)> setEffect)
    {
      m_model->UpdateEffects(setEffect);
    }

    //----------------------------------------------------------------------------
    void ModelEntry::CalculateBounds()
    {
      if (m_model->meshes.size() == 0)
      {
        return;
      }

      m_modelBounds[0] = m_model->meshes[0]->boundingBox.Center.x - m_model->meshes[0]->boundingBox.Extents.x;
      m_modelBounds[1] = m_model->meshes[0]->boundingBox.Center.x + m_model->meshes[0]->boundingBox.Extents.x;

      m_modelBounds[2] = m_model->meshes[0]->boundingBox.Center.y - m_model->meshes[0]->boundingBox.Extents.y;
      m_modelBounds[3] = m_model->meshes[0]->boundingBox.Center.y + m_model->meshes[0]->boundingBox.Extents.y;

      m_modelBounds[4] = m_model->meshes[0]->boundingBox.Center.z - m_model->meshes[0]->boundingBox.Extents.z;
      m_modelBounds[5] = m_model->meshes[0]->boundingBox.Center.z + m_model->meshes[0]->boundingBox.Extents.z;

      for (auto& mesh : m_model->meshes)
      {
        auto bbox = mesh->boundingBox;
        m_modelBounds[0] = std::fmin(m_modelBounds[0], mesh->boundingBox.Center.x - mesh->boundingBox.Extents.x);
        m_modelBounds[1] = std::fmax(m_modelBounds[1], mesh->boundingBox.Center.x + mesh->boundingBox.Extents.x);

        m_modelBounds[2] = std::fmin(m_modelBounds[2], mesh->boundingBox.Center.y - mesh->boundingBox.Extents.y);
        m_modelBounds[3] = std::fmax(m_modelBounds[3], mesh->boundingBox.Center.y + mesh->boundingBox.Extents.y);

        m_modelBounds[4] = std::fmin(m_modelBounds[4], mesh->boundingBox.Center.z - mesh->boundingBox.Extents.z);
        m_modelBounds[5] = std::fmax(m_modelBounds[5], mesh->boundingBox.Center.z + mesh->boundingBox.Extents.z);
      }
    }
  }
}