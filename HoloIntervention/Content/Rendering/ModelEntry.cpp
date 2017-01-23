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
#include "ModelEntry.h"

// Common includes
#include <DeviceResources.h>
#include <StepTimer.h>

// DirectXTK includes
#include <DirectXHelper.h>
#include <Effects.h>
#include <InstancedEffects.h>

// STL includes
#include <algorithm>

// Unnecessary, but removes intellisense errors
#include <WindowsNumerics.h>

using namespace Concurrency;
using namespace DirectX;
using namespace Microsoft::WRL;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation)
      : m_deviceResources(deviceResources)
      , m_assetLocation(assetLocation)
    {
      // Validate asset location
      Platform::String^ mainFolderLocation = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;

      auto folderTask = create_task(StorageFolder::GetFolderFromPathAsync(mainFolderLocation)).then([this](StorageFolder ^ folder)
      {
        std::string asset(m_assetLocation.begin(), m_assetLocation.end());

        char drive[32];
        char dir[32767];
        char name[2048];
        char ext[32];
        _splitpath_s(asset.c_str(), drive, dir, name, ext);

        std::string nameStr(name);
        std::string extStr(ext);
        std::string dirStr(dir);
        std::replace(dirStr.begin(), dirStr.end(), '/', '\\');
        std::wstring wdir(dirStr.begin(), dirStr.end());
        create_task(folder->GetFolderAsync(ref new Platform::String(wdir.c_str()))).then([this, nameStr, extStr](concurrency::task<StorageFolder^> previousTask)
        {
          StorageFolder^ folder;
          try
          {
            folder = previousTask.get();
          }
          catch (Platform::InvalidArgumentException^ e)
          {
            return;
          }
          catch (const std::exception&)
          {
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
                HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_ERROR, std::string("Unable to load model. ") + e.what());
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
    void ModelEntry::Update(const DX::StepTimer& timer, const DX::ViewProjection& vp)
    {
      m_viewProjection = vp;

      if (m_enableLerp)
      {
        const float deltaTime = static_cast<float>(timer.GetElapsedSeconds());

        m_worldMatrix = lerp(m_currentPose, m_desiredPose, deltaTime * m_poseLerpRate);
        m_currentPose = m_worldMatrix;
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::Render()
    {
      if (!m_loadingComplete || !m_visible)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      // TODO : add in frustrum culling

      // Draw opaque parts
      for (auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it)
      {
        auto mesh = it->get();
        assert(mesh != 0);

        mesh->PrepareForRendering(context, *m_states, false, false);

        DrawMesh(*mesh, false);
      }

      // Draw alpha parts
      for (auto it = m_model->meshes.cbegin(); it != m_model->meshes.cend(); ++it)
      {
        auto mesh = it->get();
        assert(mesh != 0);

        mesh->PrepareForRendering(context, *m_states, true, false);

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
      m_model->UpdateEffects([this](IEffect * effect)
      {
        InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
        if (basicEffect != nullptr)
        {
          XMStoreFloat4(&m_defaultColour, basicEffect->GetDiffuseColor());
          m_defaultColour.w = basicEffect->GetAlpha();
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
      m_renderingState = state;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetWorld(const float4x4& world)
    {
      if (m_enableLerp)
      {
        m_desiredPose = world;
      }
      else
      {
        m_worldMatrix = world;
      }
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
    void ModelEntry::EnablePoseLerp(bool enable)
    {
      m_enableLerp = enable;
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
    const std::array<float, 6>& ModelEntry::GetBounds() const
    {
      return m_modelBounds;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderGreyscale()
    {
      m_renderingState = RENDERING_GREYSCALE;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderDefault()
    {
      m_renderingState = RENDERING_DEFAULT;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::IsLoaded() const
    {
      return m_loadingComplete;
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
          DirectX::XMMATRIX view[2] = { DirectX::XMLoadFloat4x4(&m_viewProjection.view[0]), DirectX::XMLoadFloat4x4(&m_viewProjection.view[1]) };
          DirectX::XMMATRIX proj[2] = { DirectX::XMLoadFloat4x4(&m_viewProjection.projection[0]), DirectX::XMLoadFloat4x4(&m_viewProjection.projection[1]) };

          imatrices->SetMatrices(DirectX::XMLoadFloat4x4(&m_worldMatrix), view, proj);
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

      assert(part.effect != nullptr);
      InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(part.effect.get());
      if (basicEffect != nullptr && m_renderingState == RENDERING_GREYSCALE)
      {
        basicEffect->SetColorAndAlpha(XMLoadFloat4(&XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f)));
      }
      else if (basicEffect != nullptr && m_renderingState == RENDERING_DEFAULT)
      {
        basicEffect->SetColorAndAlpha(XMLoadFloat4(&m_defaultColour));
      }
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
        m_modelBounds[0] = min(m_modelBounds[0], mesh->boundingBox.Center.x - mesh->boundingBox.Extents.x);
        m_modelBounds[1] = max(m_modelBounds[1], mesh->boundingBox.Center.x + mesh->boundingBox.Extents.x);

        m_modelBounds[2] = min(m_modelBounds[2], mesh->boundingBox.Center.y - mesh->boundingBox.Extents.y);
        m_modelBounds[3] = max(m_modelBounds[3], mesh->boundingBox.Center.y + mesh->boundingBox.Extents.y);

        m_modelBounds[4] = min(m_modelBounds[4], mesh->boundingBox.Center.z - mesh->boundingBox.Extents.z);
        m_modelBounds[5] = max(m_modelBounds[5], mesh->boundingBox.Center.z + mesh->boundingBox.Extents.z);
      }
    }
  }
}