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
#include "ModelRenderer.h"
#include "RenderingCommon.h"
#include "StepTimer.h"

// DirectXTK includes
#include <CommonStates.h>
#include <DirectXHelper.h>
#include <Effects.h>
#include <InstancedEffects.h>
#include <InstancedGeometricPrimitive.h>
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
    ModelEntry::ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation, DX::StepTimer& timer, Debug& debug)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
      , m_debug(debug)
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

          create_task(folder->GetFileAsync(ref new Platform::String(wFilename.c_str()))).then([ this ](task<StorageFile^> fileTask)
          {
            StorageFile^ file(nullptr);
            try
            {
              file = fileTask.get();
            }
            catch (Platform::Exception^ e)
            {
              WLOG_ERROR(L"Unable to open file: " + e->Message);
              m_failedLoad = true;
            }

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
              }
            }
            else
            {
              m_failedLoad = true;
            }
          });
        });
      });
    }

    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, PrimitiveType type, DX::StepTimer& timer, Debug& debug, float3 argument, size_t tessellation, bool rhcoords, bool invertn, float4 colour)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
      , m_debug(debug)
      , m_originalColour(colour)
      , m_currentColour(colour)
      , m_primitiveType(type)
      , m_tessellation(tessellation)
      , m_rhcoords(rhcoords)
      , m_invertn(invertn)
      , m_argument(argument)
    {
      try
      {
        CreateDeviceDependentResources();
      }
      catch (const std::exception& e)
      {
        HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load primitive. ") + e.what());
        m_failedLoad = true;
        return;
      }
    }

    //----------------------------------------------------------------------------
    ModelEntry::ModelEntry(const std::shared_ptr<DX::DeviceResources>& deviceResources, UWPOpenIGTLink::Polydata^ polydata, DX::StepTimer& timer, Debug& debug)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
      , m_debug(debug)
    {


      try
      {
        CreateDeviceDependentResources();
      }
      catch (const std::exception& e)
      {
        HoloIntervention::LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load primitive. ") + e.what());
        m_failedLoad = true;
        return;
      }
    }

    //----------------------------------------------------------------------------
    ModelEntry::~ModelEntry()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<ModelEntry> ModelEntry::Clone()
    {
      std::shared_ptr<ModelEntry> newEntry;
      if (m_primitive != nullptr)
      {
        newEntry = std::make_shared<ModelEntry>(m_deviceResources, m_primitiveType, m_timer, m_debug, m_argument, m_tessellation, m_rhcoords, m_invertn);
      }
      else
      {
        newEntry = std::make_shared<ModelEntry>(m_deviceResources, m_assetLocation, m_timer, m_debug);
      }
      newEntry->m_originalColour = m_originalColour;
      newEntry->m_originalColour = m_currentColour;
      newEntry->m_modelBounds = m_modelBounds;
      newEntry->m_wireframe = m_wireframe ? true : false;
      newEntry->m_velocity = m_velocity;
      newEntry->m_lastPose = m_lastPose;
      newEntry->m_currentPose = m_currentPose;
      newEntry->m_desiredPose = m_desiredPose;
      newEntry->m_visible = m_visible ? true : false;
      newEntry->m_enableLerp = m_enableLerp ? true : false;
      newEntry->m_isInFrustum = m_isInFrustum ? true : false;
      newEntry->m_frustumCheckFrameNumber = m_frustumCheckFrameNumber;
      newEntry->m_poseLerpRate = m_poseLerpRate;
      newEntry->m_id = INVALID_TOKEN;

      return newEntry;
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

      if (m_primitive != nullptr)
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
        m_primitive->Draw(XMLoadFloat4x4(&m_currentPose), view, projection, XMLoadFloat4(&m_currentColour));
      }
      else
      {
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
    }

    //----------------------------------------------------------------------------
    void ModelEntry::CreateDeviceDependentResources()
    {
      if (m_primitiveType != PrimitiveType_NONE)
      {
        m_primitive = ModelRenderer::CreatePrimitive(*m_deviceResources, m_primitiveType, m_argument, m_tessellation, m_rhcoords, m_invertn);

        if (m_primitive == nullptr)
        {
          LOG_ERROR(L"Unable to create primitive, unknown type.");
        }
      }
      else
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
      }
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
      m_primitive = nullptr;
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
    bool ModelEntry::IsPrimitive() const
    {
      return m_primitive != nullptr;
    }

    //----------------------------------------------------------------------------
    HoloIntervention::Rendering::PrimitiveType ModelEntry::GetPrimitiveType() const
    {
      return m_primitiveType;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 ModelEntry::GetArgument() const
    {
      return m_argument;
    }

    //----------------------------------------------------------------------------
    size_t ModelEntry::GetTessellation() const
    {
      return m_tessellation;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::GetRHCoords() const
    {
      return m_rhcoords;
    }

    //----------------------------------------------------------------------------
    bool ModelEntry::GetInvertN() const
    {
      return m_invertn;
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
      if (m_model != nullptr)
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
      else
      {
        m_currentColour = float4(1.f, 1.f, 1.f, 1.f);
      }
    }

    //----------------------------------------------------------------------------
    void ModelEntry::RenderDefault()
    {
      if (m_model != nullptr)
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
      else
      {
        m_currentColour = m_originalColour;
      }
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
    void ModelEntry::SetColour(float3 newColour)
    {
      m_currentColour = float4(newColour.x, newColour.y, newColour.z, m_currentColour.w);
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetColour(float r, float g, float b, float a)
    {
      m_currentColour = float4(r, g, b, a);
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetColour(Windows::Foundation::Numerics::float4 newColour)
    {
      m_currentColour = newColour;
    }

    //----------------------------------------------------------------------------
    void ModelEntry::SetColour(float r, float g, float b)
    {
      m_currentColour = float4{ r, g, b, m_currentColour.w };
    }

    //----------------------------------------------------------------------------
    float4 ModelEntry::GetCurrentColour() const
    {
      return m_currentColour;
    }

    //----------------------------------------------------------------------------
    float4 ModelEntry::GetOriginalColour() const
    {
      return m_originalColour;
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
    std::unique_ptr<DirectX::Model> ModelEntry::CreateFromPolyData(ID3D11Device* d3dDevice, UWPOpenIGTLink::Polydata^ polyData)
    {
      std::unique_ptr<Model> model(new Model());

      polyData->Indices
      polyData->Normals
      polyData->Points
      polyData->Colours
      polyData->TextureCoords

      for (UINT meshIndex = 0; meshIndex < *nMesh; ++meshIndex)
      {
        // Mesh name
        auto mesh = std::make_shared<ModelMesh>();
        mesh->name = L"PolyDataMesh";
        mesh->ccw = true;
        mesh->pmalpha = false;

        // Materials
        std::vector<MaterialRecordCMO> materials;
        materials.reserve(*nMats);
        for (UINT j = 0; j < *nMats; ++j)
        {
          MaterialRecordCMO m;

          // Material name
          nName = reinterpret_cast<const UINT*>(meshData + usedSize);
          auto matName = reinterpret_cast<const wchar_t*>(meshData + usedSize);
          m.name.assign(matName, *nName);

          // Material settings
          auto matSetting = reinterpret_cast<const VSD3DStarter::Material*>(meshData + usedSize);
          m.pMaterial = matSetting;

          // Pixel shader name
          nName = reinterpret_cast<const UINT*>(meshData + usedSize);
          auto psName = reinterpret_cast<const wchar_t*>(meshData + usedSize);

          m.pixelShader.assign(psName, *nName);

          for (UINT t = 0; t < VSD3DStarter::MAX_TEXTURE; ++t)
          {
            nName = reinterpret_cast<const UINT*>(meshData + usedSize);
            auto txtName = reinterpret_cast<const wchar_t*>(meshData + usedSize);
            m.texture[t].assign(txtName, *nName);
          }

          materials.emplace_back(m);
        }

        if (materials.empty())
        {
          // Add default material if none defined
          MaterialRecordCMO m;
          m.pMaterial = &VSD3DStarter::s_defMaterial;
          m.name = L"Default";
          materials.emplace_back(m);
        }

        // Skeletal data?
        auto bSkeleton = reinterpret_cast<const BYTE*>(meshData + usedSize);

        // Submeshes
        auto nSubmesh = reinterpret_cast<const UINT*>(meshData + usedSize);
        if (!*nSubmesh)
        {
          throw std::exception("No submeshes found\n");
        }

        auto subMesh = reinterpret_cast<const VSD3DStarter::SubMesh*>(meshData + usedSize);

        // Index buffers
        auto nIBs = reinterpret_cast<const UINT*>(meshData + usedSize);
        if (!*nIBs)
        {
          throw std::exception("No index buffers found\n");
        }

        struct IBData
        {
          size_t          nIndices;
          const USHORT*   ptr;
        };

        std::vector<IBData> ibData;
        ibData.reserve(*nIBs);

        std::vector<ComPtr<ID3D11Buffer>> ibs;
        ibs.resize(*nIBs);

        for (UINT j = 0; j < *nIBs; ++j)
        {
          auto nIndexes = reinterpret_cast<const UINT*>(meshData + usedSize);
          if (!*nIndexes)
          {
            throw std::exception("Empty index buffer found\n");
          }

          size_t ibBytes = sizeof(USHORT) * (*(nIndexes));

          auto indexes = reinterpret_cast<const USHORT*>(meshData + usedSize);

          IBData ib;
          ib.nIndices = *nIndexes;
          ib.ptr = indexes;
          ibData.emplace_back(ib);

          D3D11_BUFFER_DESC desc = {};
          desc.Usage = D3D11_USAGE_DEFAULT;
          desc.ByteWidth = static_cast<UINT>(ibBytes);
          desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

          D3D11_SUBRESOURCE_DATA initData = {};
          initData.pSysMem = indexes;

          DX::ThrowIfFailed(d3dDevice->CreateBuffer(&desc, &initData, &ibs[j]));

          SetDebugObjectName(ibs[j].Get(), "ModelCMO");
        }

        assert(ibData.size() == *nIBs);
        assert(ibs.size() == *nIBs);

        // Vertex buffers
        auto nVBs = reinterpret_cast<const UINT*>(meshData + usedSize);
        if (!*nVBs)
        {
          throw std::exception("No vertex buffers found\n");
        }

        struct VBData
        {
          size_t                                          nVerts;
          const VertexPositionNormalTangentColorTexture*  ptr;
          const VSD3DStarter::SkinningVertex*             skinPtr;
        };

        std::vector<VBData> vbData;
        vbData.reserve(*nVBs);
        for (UINT j = 0; j < *nVBs; ++j)
        {
          auto nVerts = reinterpret_cast<const UINT*>(meshData + usedSize);
          if (!*nVerts)
          {
            throw std::exception("Empty vertex buffer found\n");
          }

          size_t vbBytes = sizeof(VertexPositionNormalTangentColorTexture) * (*(nVerts));

          auto verts = reinterpret_cast<const VertexPositionNormalTangentColorTexture*>(meshData + usedSize);

          VBData vb;
          vb.nVerts = *nVerts;
          vb.ptr = verts;
          vb.skinPtr = nullptr;
          vbData.emplace_back(vb);
        }

        assert(vbData.size() == *nVBs);

        // Skinning vertex buffers
        auto nSkinVBs = reinterpret_cast<const UINT*>(meshData + usedSize);

        if (*nSkinVBs)
        {
          if (*nSkinVBs != *nVBs)
          {
            throw std::exception("Number of VBs not equal to number of skin VBs");
          }

          for (UINT j = 0; j < *nSkinVBs; ++j)
          {
            auto nVerts = reinterpret_cast<const UINT*>(meshData + usedSize);
            if (!*nVerts)
            {
              throw std::exception("Empty skinning vertex buffer found\n");
            }

            if (vbData[j].nVerts != *nVerts)
            {
              throw std::exception("Mismatched number of verts for skin VBs");
            }

            size_t vbBytes = sizeof(VSD3DStarter::SkinningVertex) * (*(nVerts));

            auto verts = reinterpret_cast<const VSD3DStarter::SkinningVertex*>(meshData + usedSize);
            vbData[j].skinPtr = verts;
          }
        }

        // Extents
        auto extents = reinterpret_cast<const VSD3DStarter::MeshExtents*>(meshData + usedSize);

        mesh->boundingSphere.Center.x = extents->CenterX;
        mesh->boundingSphere.Center.y = extents->CenterY;
        mesh->boundingSphere.Center.z = extents->CenterZ;
        mesh->boundingSphere.Radius = extents->Radius;

        XMVECTOR min = XMVectorSet(extents->MinX, extents->MinY, extents->MinZ, 0.f);
        XMVECTOR max = XMVectorSet(extents->MaxX, extents->MaxY, extents->MaxZ, 0.f);
        BoundingBox::CreateFromPoints(mesh->boundingBox, min, max);

        UNREFERENCED_PARAMETER(bSkeleton);

        bool enableSkinning = (*nSkinVBs) != 0;

        // Build vertex buffers
        std::vector<ComPtr<ID3D11Buffer>> vbs;
        vbs.resize(*nVBs);

        const size_t stride = enableSkinning ? sizeof(VertexPositionNormalTangentColorTextureSkinning)
                              : sizeof(VertexPositionNormalTangentColorTexture);

        for (UINT j = 0; j < *nVBs; ++j)
        {
          size_t nVerts = vbData[j].nVerts;

          size_t bytes = stride * nVerts;

          D3D11_BUFFER_DESC desc = {};
          desc.Usage = D3D11_USAGE_DEFAULT;
          desc.ByteWidth = static_cast<UINT>(bytes);
          desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

          std::unique_ptr<uint8_t[]> temp(new uint8_t[bytes + (sizeof(UINT) * nVerts)]);

          auto visited = reinterpret_cast<UINT*>(temp.get() + bytes);
          memset(visited, 0xff, sizeof(UINT) * nVerts);

          assert(vbData[j].ptr != 0);

          if (enableSkinning)
          {
            // Combine CMO multi-stream data into a single stream
            auto skinptr = vbData[j].skinPtr;
            assert(skinptr != 0);

            uint8_t* ptr = temp.get();

            auto sptr = vbData[j].ptr;

            for (size_t v = 0; v < nVerts; ++v)
            {
              *reinterpret_cast<VertexPositionNormalTangentColorTexture*>(ptr) = *sptr;
              ++sptr;

              auto skinv = reinterpret_cast<VertexPositionNormalTangentColorTextureSkinning*>(ptr);
              skinv->SetBlendIndices(*reinterpret_cast<const XMUINT4*>(skinptr->boneIndex));
              skinv->SetBlendWeights(*reinterpret_cast<const XMFLOAT4*>(skinptr->boneWeight));

              ptr += stride;
            }
          }
          else
          {
            memcpy(temp.get(), vbData[j].ptr, bytes);
          }

          // Need to fix up VB tex coords for UV transform which is not supported by basic effects
          for (UINT k = 0; k < *nSubmesh; ++k)
          {
            auto& sm = subMesh[k];

            if (sm.VertexBufferIndex != j)
            {
              continue;
            }

            if ((sm.IndexBufferIndex >= *nIBs)
                || (sm.MaterialIndex >= materials.size()))
            {
              throw std::exception("Invalid submesh found\n");
            }

            XMMATRIX uvTransform = XMLoadFloat4x4(&materials[sm.MaterialIndex].pMaterial->UVTransform);

            auto ib = ibData[sm.IndexBufferIndex].ptr;

            size_t count = ibData[sm.IndexBufferIndex].nIndices;

            for (size_t q = 0; q < count; ++q)
            {
              size_t v = ib[q];

              if (v >= nVerts)
              {
                throw std::exception("Invalid index found\n");
              }

              auto verts = reinterpret_cast<VertexPositionNormalTangentColorTexture*>(temp.get() + (v * stride));
              if (visited[v] == UINT(-1))
              {
                visited[v] = sm.MaterialIndex;

                XMVECTOR t = XMLoadFloat2(&verts->textureCoordinate);

                t = XMVectorSelect(g_XMIdentityR3, t, g_XMSelect1110);

                t = XMVector4Transform(t, uvTransform);

                XMStoreFloat2(&verts->textureCoordinate, t);
              }
              else if (visited[v] != sm.MaterialIndex)
              {
#ifdef _DEBUG
                XMMATRIX uv2 = XMLoadFloat4x4(&materials[visited[v]].pMaterial->UVTransform);

                if (XMVector4NotEqual(uvTransform.r[0], uv2.r[0])
                    || XMVector4NotEqual(uvTransform.r[1], uv2.r[1])
                    || XMVector4NotEqual(uvTransform.r[2], uv2.r[2])
                    || XMVector4NotEqual(uvTransform.r[3], uv2.r[3]))
                {
                  DebugTrace("WARNING: %ls - mismatched UV transforms for the same vertex; texture coordinates may not be correct\n", mesh->name.c_str());
                }
#endif
              }
            }

            // Create vertex buffer from temporary buffer
            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = temp.get();

            ThrowIfFailed(
              d3dDevice->CreateBuffer(&desc, &initData, &vbs[j])
            );
          }

          SetDebugObjectName(vbs[j].Get(), "ModelCMO");
        }

        assert(vbs.size() == *nVBs);

        // Create Effects
        for (size_t j = 0; j < materials.size(); ++j)
        {
          auto& m = materials[j];

          EffectFactory::EffectInfo info;
          info.name = m.name.c_str();
          info.specularPower = m.pMaterial->SpecularPower;
          info.perVertexColor = true;
          info.enableSkinning = enableSkinning;
          info.alpha = m.pMaterial->Diffuse.w;
          info.ambientColor = XMFLOAT3(m.pMaterial->Ambient.x, m.pMaterial->Ambient.y, m.pMaterial->Ambient.z);
          info.diffuseColor = XMFLOAT3(m.pMaterial->Diffuse.x, m.pMaterial->Diffuse.y, m.pMaterial->Diffuse.z);
          info.specularColor = XMFLOAT3(m.pMaterial->Specular.x, m.pMaterial->Specular.y, m.pMaterial->Specular.z);
          info.emissiveColor = XMFLOAT3(m.pMaterial->Emissive.x, m.pMaterial->Emissive.y, m.pMaterial->Emissive.z);
          info.diffuseTexture = m.texture[0].c_str();

          m.effect = fxFactory.CreateEffect(info, nullptr);

          CreateInputLayout(d3dDevice, m.effect.get(), &m.il, enableSkinning);
        }

        // Build mesh parts
        for (UINT j = 0; j < *nSubmesh; ++j)
        {
          auto& sm = subMesh[j];

          if ((sm.IndexBufferIndex >= *nIBs)
              || (sm.VertexBufferIndex >= *nVBs)
              || (sm.MaterialIndex >= materials.size()))
          {
            throw std::exception("Invalid submesh found\n");
          }

          auto& mat = materials[sm.MaterialIndex];

          auto part = new ModelMeshPart();

          if (mat.pMaterial->Diffuse.w < 1)
          {
            part->isAlpha = true;
          }

          part->indexCount = sm.PrimCount * 3;
          part->startIndex = sm.StartIndex;
          part->vertexStride = static_cast<UINT>(stride);
          part->inputLayout = mat.il;
          part->indexBuffer = ibs[sm.IndexBufferIndex];
          part->vertexBuffer = vbs[sm.VertexBufferIndex];
          part->effect = mat.effect;
          part->vbDecl = enableSkinning ? g_vbdeclSkinning : g_vbdecl;

          mesh->meshParts.emplace_back(part);
        }

        model->meshes.emplace_back(mesh);
      }

      return model;
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