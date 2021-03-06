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
#include "Common.h"
#include "CameraResources.h"
#include "DeviceResources.h"
#include "Model.h"
#include "ModelRenderer.h"
#include "RenderingCommon.h"
#include "StepTimer.h"

// DirectXTK includes
#include <CommonStates.h>
#include <DirectXHelper.h>
#include <DirectXHelpers.h>
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

namespace DirectX
{
  //----------------------------------------------------------------------------
  // Helper for creating a D3D input layout.
  static void CreateInputLayout(_In_ ID3D11Device* device, IEffect* effect, _Out_ ID3D11InputLayout** pInputLayout)
  {
    void const* shaderByteCode;
    size_t byteCodeLength;

    effect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    DX::ThrowIfFailed(
      device->CreateInputLayout(VertexPositionNormalColorTexture::InputElements,
                                VertexPositionNormalColorTexture::InputElementCount,
                                shaderByteCode, byteCodeLength,
                                pInputLayout)
    );
    _Analysis_assume_(*pInputLayout != 0);

#if defined(_DEBUG)
    SetDebugObjectName(*pInputLayout, "ModelPolyData");
#endif
  }

  //----------------------------------------------------------------------------
  std::unique_ptr<DirectX::Model> CreateFromPolyData(ID3D11Device* d3dDevice, IEffectFactory& fxFactory, UWPOpenIGTLink::Polydata^ polyData)
  {
    std::unique_ptr<Model> model(new Model());

    // Mesh name
    auto mesh = std::make_shared<ModelMesh>();
    mesh->name = L"PolyDataMesh";
    mesh->ccw = true;
    mesh->pmalpha = false;

    // Indices
    size_t ibBytes = sizeof(USHORT) * polyData->Indices->Size;

    ComPtr<ID3D11Buffer> indexBuffer;
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.ByteWidth = static_cast<UINT>(ibBytes);
    buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA indexBufferInitData = {};
    std::vector<uint16> indices;
    std::copy(begin(polyData->Indices), end(polyData->Indices), begin(indices));
    indexBufferInitData.pSysMem = &indices.front();

    DX::ThrowIfFailed(d3dDevice->CreateBuffer(&buffer_desc, &indexBufferInitData, &indexBuffer));
#if defined(_DEBUG)
    DirectX::SetDebugObjectName(indexBuffer.Get(), "ModelPolyDataIndex");
#endif

    // Determine extents while constructing vertex entries
    float minX = polyData->Points->GetAt(0).x;
    float maxX = polyData->Points->GetAt(0).x;
    float minY = polyData->Points->GetAt(0).y;
    float maxY = polyData->Points->GetAt(0).y;
    float minZ = polyData->Points->GetAt(0).z;
    float maxZ = polyData->Points->GetAt(0).z;

    std::vector<VertexPositionNormalColorTexture> vertices;
    for (uint32 i = 0; i < polyData->Points->Size; ++i)
    {
      VertexPositionNormalColorTexture entry;
      entry.position = XMFLOAT3(polyData->Points->GetAt(i).x, polyData->Points->GetAt(i).y, polyData->Points->GetAt(i).z);

      entry.normal = XMFLOAT3(0.f, 0.f, 0.f);
      if (polyData->Normals->Size == polyData->Points->Size)
      {
        entry.normal = XMFLOAT3(polyData->Normals->GetAt(i).x, polyData->Normals->GetAt(i).y, polyData->Normals->GetAt(i).z);
      }
      entry.color = XMFLOAT4(1.f, 1.f, 1.f, 1.f);
      if (polyData->Colours->Size == polyData->Colours->Size)
      {
        entry.color = XMFLOAT4(polyData->Colours->GetAt(i).x, polyData->Colours->GetAt(i).y, polyData->Colours->GetAt(i).z, polyData->Colours->GetAt(i).w);
      }
      entry.textureCoordinate = XMFLOAT2(0.f, 0.f);
      if (polyData->TextureCoords->Size == polyData->TextureCoords->Size)
      {
        entry.textureCoordinate = XMFLOAT2(polyData->TextureCoords->GetAt(i).x, polyData->TextureCoords->GetAt(i).y);
      }

      minX = entry.position.x < minX ? entry.position.x : minX;
      maxX = entry.position.x > maxX ? entry.position.x : maxX;

      minY = entry.position.y < minY ? entry.position.y : minY;
      maxY = entry.position.y > maxY ? entry.position.y : maxY;

      minZ = entry.position.z < minZ ? entry.position.z : minZ;
      maxZ = entry.position.z > maxZ ? entry.position.z : maxZ;
    }

    // Extents
    mesh->boundingSphere.Center.x = (minX + maxX) / 2.f;
    mesh->boundingSphere.Center.y = (minY + maxY) / 2.f;
    mesh->boundingSphere.Center.z = (minZ + maxZ) / 2.f;
    mesh->boundingSphere.Radius = length(float3(maxX, maxY, maxZ) - float3(mesh->boundingSphere.Center.x, mesh->boundingSphere.Center.y, mesh->boundingSphere.Center.z));

    XMVECTOR min = XMVectorSet(minX, minY, minZ, 0.f);
    XMVECTOR max = XMVectorSet(maxX, maxY, maxZ, 0.f);
    BoundingBox::CreateFromPoints(mesh->boundingBox, min, max);

    // Build vertex buffers
    ComPtr<ID3D11Buffer> vertexBuffer;
    const size_t stride = sizeof(VertexPositionNormalColorTexture);

    size_t nVerts = vertices.size();
    size_t bytes = stride * nVerts;

    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.ByteWidth = static_cast<UINT>(bytes);
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    // Create vertex buffer from temporary buffer
    D3D11_SUBRESOURCE_DATA vertexBufferInitData = {};
    vertexBufferInitData.pSysMem = &vertices.front();

    DX::ThrowIfFailed(d3dDevice->CreateBuffer(&buffer_desc, &vertexBufferInitData, &vertexBuffer));
#if defined(_DEBUG)
    SetDebugObjectName(vertexBuffer.Get(), "ModelPolyData");
#endif

    // Create Effects
    EffectFactory::EffectInfo info;
    info.name = polyData->Mat->Name->Data();
    info.specularPower = polyData->Mat->SpecularExponent;
    info.perVertexColor = true;
    info.enableSkinning = false;
    info.alpha = polyData->Mat->Diffuse.w;
    info.ambientColor = XMFLOAT3(polyData->Mat->Ambient.x, polyData->Mat->Ambient.y, polyData->Mat->Ambient.z);
    info.diffuseColor = XMFLOAT3(polyData->Mat->Diffuse.x, polyData->Mat->Diffuse.y, polyData->Mat->Diffuse.z);
    info.specularColor = XMFLOAT3(polyData->Mat->Specular.x, polyData->Mat->Specular.y, polyData->Mat->Specular.z);
    info.emissiveColor = XMFLOAT3(polyData->Mat->Emissive.x, polyData->Mat->Emissive.y, polyData->Mat->Emissive.z);
    info.diffuseTexture = nullptr;

    auto effect = fxFactory.CreateEffect(info, nullptr);

    ComPtr<ID3D11InputLayout> il;
    CreateInputLayout(d3dDevice, effect.get(), il.GetAddressOf());

    // Build mesh parts
    auto part = std::unique_ptr<ModelMeshPart>(new ModelMeshPart());

    if (info.alpha < 1)
    {
      part->isAlpha = true;
    }

    part->indexCount = polyData->Indices->Size;
    part->startIndex = 0;
    part->vertexStride = static_cast<UINT>(stride);
    part->inputLayout = il;
    part->indexBuffer = indexBuffer;
    part->indexFormat = DXGI_FORMAT_R16_UINT;
    part->vertexBuffer = vertexBuffer;
    part->effect = effect;
    auto vec = std::make_shared<std::vector<D3D11_INPUT_ELEMENT_DESC>>(VertexPositionNormalColorTexture::InputElements, VertexPositionNormalColorTexture::InputElements + VertexPositionNormalColorTexture::InputElementCount);
    part->vbDecl = vec;

    mesh->meshParts.push_back(std::move(part));

    return model;
  }
}

namespace HoloIntervention
{
  namespace Rendering
  {
    //----------------------------------------------------------------------------
    Model::Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, const std::wstring& assetLocation, DX::StepTimer& timer, Debug& debug)
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
          LOG(LogLevelType::LOG_LEVEL_ERROR, "Unable to locate installed folder path.");
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
            LOG(LogLevelType::LOG_LEVEL_ERROR, L"InvalidArgumentException: " + e->Message);
            m_failedLoad = true;
            return;
          }
          catch (const std::exception& e)
          {
            LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to get subfolder: ") + e.what());
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
                LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load model. ") + e.what());
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
    Model::Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, PrimitiveType type, DX::StepTimer& timer, Debug& debug, float3 argument, size_t tessellation, bool rhcoords, bool invertn, float4 colour)
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
        LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load primitive. ") + e.what());
        m_failedLoad = true;
        return;
      }
    }

    //----------------------------------------------------------------------------
    Model::Model(const std::shared_ptr<DX::DeviceResources>& deviceResources, UWPOpenIGTLink::Polydata^ polydata, DX::StepTimer& timer, Debug& debug)
      : m_deviceResources(deviceResources)
      , m_timer(timer)
      , m_debug(debug)
    {
      m_polydata = polydata;

      try
      {
        CreateDeviceDependentResources();
      }
      catch (const std::exception& e)
      {
        LOG(LogLevelType::LOG_LEVEL_ERROR, std::string("Unable to load primitive. ") + e.what());
        m_failedLoad = true;
        return;
      }
    }

    //----------------------------------------------------------------------------
    Model::~Model()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Model> Model::Clone()
    {
      std::shared_ptr<Model> newEntry;
      if (m_primitive != nullptr)
      {
        newEntry = std::make_shared<Model>(m_deviceResources, m_primitiveType, m_timer, m_debug, m_argument, m_tessellation, m_rhcoords, m_invertn);
      }
      else
      {
        newEntry = std::make_shared<Model>(m_deviceResources, m_assetLocation, m_timer, m_debug);
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
      newEntry->m_cameraResources = m_cameraResources;
      newEntry->m_id = INVALID_TOKEN;

      return newEntry;
    }

    //----------------------------------------------------------------------------
    void Model::Update(const DX::CameraResources* cameraResources)
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
    void Model::Render()
    {
      if (!m_loadingComplete || !m_visible)
      {
        return;
      }

      const auto context = m_deviceResources->GetD3DDeviceContext();

      if (m_primitive != nullptr)
      {
        m_primitive->Draw(XMLoadFloat4x4(&m_currentPose),
                          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().hmdToView[0])),
                          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().hmdToView[1])),
                          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[0])),
                          XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[1])),
                          XMLoadFloat4(&m_currentColour)
                         );
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
      }

      // Clean up after rendering
      context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
      context->OMSetDepthStencilState(nullptr, 0);
      context->RSSetState(nullptr);
    }

    //----------------------------------------------------------------------------
    void Model::CreateDeviceDependentResources()
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
        m_effectFactory->SetDirectory((Windows::ApplicationModel::Package::Current->InstalledLocation->Path + L"\\Assets\\Textures")->Data());
        if (m_polydata != nullptr)
        {
          m_model = std::shared_ptr<DirectX::Model>(std::move(CreateFromPolyData(m_deviceResources->GetD3DDevice(), *m_effectFactory, m_polydata)));
        }
        else
        {
          m_model = std::shared_ptr<DirectX::Model>(std::move(DirectX::Model::CreateFromCMO(m_deviceResources->GetD3DDevice(), m_assetLocation.c_str(), *m_effectFactory)));
        }

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
      CalculateBounds();

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void Model::ReleaseDeviceDependentResources()
    {
      m_loadingComplete = false;

      m_modelBounds = { -1.f };
      m_model = nullptr;
      m_effectFactory = nullptr;
      m_states = nullptr;
      m_primitive = nullptr;
    }

    //----------------------------------------------------------------------------
    void Model::SetVisible(bool enable)
    {
      m_visible = enable;
    }

    //----------------------------------------------------------------------------
    void Model::ToggleVisible()
    {
      m_visible = !m_visible;
    }

    //----------------------------------------------------------------------------
    bool Model::IsVisible() const
    {
      return m_visible;
    }

    //----------------------------------------------------------------------------
    bool Model::IsPrimitive() const
    {
      return m_primitive != nullptr;
    }

    //----------------------------------------------------------------------------
    HoloIntervention::Rendering::PrimitiveType Model::GetPrimitiveType() const
    {
      return m_primitiveType;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float3 Model::GetArgument() const
    {
      return m_argument;
    }

    //----------------------------------------------------------------------------
    size_t Model::GetTessellation() const
    {
      return m_tessellation;
    }

    //----------------------------------------------------------------------------
    bool Model::GetRHCoords() const
    {
      return m_rhcoords;
    }

    //----------------------------------------------------------------------------
    bool Model::GetInvertN() const
    {
      return m_invertn;
    }

    //----------------------------------------------------------------------------
    void Model::SetRenderingState(ModelRenderingState state)
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
    void Model::SetDesiredPose(const float4x4& world)
    {
      m_desiredPose = world;
    }

    //----------------------------------------------------------------------------
    void Model::SetCurrentPose(const Windows::Foundation::Numerics::float4x4& world)
    {
      m_currentPose = m_desiredPose = world;
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 Model::GetCurrentPose() const
    {
      return m_currentPose;
    }

    //----------------------------------------------------------------------------
    float3 Model::GetVelocity() const
    {
      return m_velocity;
    }

    //----------------------------------------------------------------------------
    void Model::EnableLighting(bool enable)
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
    void Model::SetCullMode(D3D11_CULL_MODE mode)
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
    bool Model::FailedLoad() const
    {
      return m_failedLoad;
    }

    //----------------------------------------------------------------------------
    void Model::SetPoseLerpRate(float lerpRate)
    {
      m_poseLerpRate = lerpRate;
    }

    //----------------------------------------------------------------------------
    uint64 Model::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    void Model::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    std::array<float, 6> Model::GetBounds(float4x4 userMatrix /* = float4x4::identity() */) const
    {
      if (userMatrix == float4x4::identity())
      {
        return m_modelBounds;
      }

      // Transform bounds into corners, transform corners, re-do bounds along origin axes
      std::array<std::array<float, 3>, 8> corners =
      {
        std::array<float, 3>{m_modelBounds[0], m_modelBounds[2], m_modelBounds[4]},
        std::array<float, 3>{m_modelBounds[0], m_modelBounds[3], m_modelBounds[4]},
        std::array<float, 3>{m_modelBounds[1], m_modelBounds[3], m_modelBounds[4]},
        std::array<float, 3>{m_modelBounds[1], m_modelBounds[2], m_modelBounds[4]},

        std::array<float, 3>{m_modelBounds[0], m_modelBounds[2], m_modelBounds[5]},
        std::array<float, 3>{m_modelBounds[0], m_modelBounds[3], m_modelBounds[5]},
        std::array<float, 3>{m_modelBounds[1], m_modelBounds[3], m_modelBounds[5]},
        std::array<float, 3>{m_modelBounds[1], m_modelBounds[2], m_modelBounds[5]}
      };

      for (std::array<std::array<float, 3>, 8>::size_type i = 0; i < corners.size(); ++i)
      {
        float3 corner(corners[i][0], corners[i][1], corners[i][2]);
        corner = transform(corner, userMatrix);
        corners[i][0] = corner.x;
        corners[i][1] = corner.y;
        corners[i][2] = corner.z;
      }

      std::array<float, 6> bounds =
      {
        corners[0][0], // Xmin
        corners[0][0], // Xmax
        corners[0][1], // Ymin
        corners[0][1], // Ymax
        corners[0][2], // Zmin
        corners[0][2]  // Zmax
      };

      for (std::array<float, 6>::size_type axis = 0; axis < bounds.size() / 2; ++axis)
      {
        // Find dimension min
        for (std::array<std::array<float, 3>, 8>::size_type i = 0; i < corners.size(); ++i)
        {
          if (corners[i][axis] <= bounds[axis * 2])
          {
            bounds[axis * 2] = corners[i][axis];
          }
        }

        // Find dimension max
        for (std::array<std::array<float, 3>, 8>::size_type i = 0; i < corners.size(); ++i)
        {
          if (corners[i][axis] >= bounds[(axis * 2) + 1])
          {
            bounds[(axis * 2) + 1] = corners[i][axis];
          }
        }
      }

      return bounds;
    }

    //----------------------------------------------------------------------------
    std::wstring Model::GetAssetLocation() const
    {
      return m_assetLocation;
    }

    //----------------------------------------------------------------------------
    bool Model::GetLerpEnabled() const
    {
      return m_enableLerp;
    }

    //----------------------------------------------------------------------------
    float Model::GetLerpRate() const
    {
      return m_poseLerpRate;
    }

    //----------------------------------------------------------------------------
    void Model::RenderGreyscale()
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
    void Model::RenderDefault()
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
    void Model::SetWireframe(bool wireframe)
    {
      m_wireframe = wireframe;
    }

    //----------------------------------------------------------------------------
    bool Model::IsLoaded() const
    {
      return m_loadingComplete;
    }

    //----------------------------------------------------------------------------
    void Model::SetColour(float3 newColour)
    {
      SetColour(float4(newColour.x, newColour.y, newColour.z, m_currentColour.w));
    }

    //----------------------------------------------------------------------------
    void Model::SetColour(float r, float g, float b, float a)
    {
      SetColour(float4(r, g, b, a));
    }

    //----------------------------------------------------------------------------
    void Model::SetColour(Windows::Foundation::Numerics::float4 newColour)
    {
      if (m_currentColour == newColour)
      {
        return;
      }

      m_currentColour = newColour;

      if (m_model != nullptr)
      {
        m_model->UpdateEffects([this, newColour](IEffect * effect)
        {
          InstancedBasicEffect* basicEffect = dynamic_cast<InstancedBasicEffect*>(effect);
          if (basicEffect != nullptr)
          {
            basicEffect->SetColorAndAlpha(XMLoadFloat4(&newColour));
          }
        });
      }
    }

    //----------------------------------------------------------------------------
    void Model::SetColour(float r, float g, float b)
    {
      SetColour(float4{ r, g, b, m_currentColour.w });
    }

    //----------------------------------------------------------------------------
    void Model::SetOriginalColour(Windows::Foundation::Numerics::float4 newColour)
    {
      m_originalColour = newColour;
    }

    //----------------------------------------------------------------------------
    void Model::SetOriginalColour(Windows::Foundation::Numerics::float3 newColour)
    {
      m_originalColour = float4(newColour.x, newColour.y, newColour.z, m_originalColour.w);
    }

    //----------------------------------------------------------------------------
    void Model::SetOriginalColour(float r, float g, float b, float a)
    {
      m_originalColour = float4(r, g, b, a);
    }

    //----------------------------------------------------------------------------
    void Model::SetOriginalColour(float r, float g, float b)
    {
      m_originalColour = float4(r, g, b, m_originalColour.w);
    }

    //----------------------------------------------------------------------------
    float4 Model::GetCurrentColour() const
    {
      return m_currentColour;
    }

    //----------------------------------------------------------------------------
    float4 Model::GetOriginalColour() const
    {
      return m_originalColour;
    }

    //----------------------------------------------------------------------------
    bool Model::IsInFrustum() const
    {
      // TODO : this is a cached value, so in theory this could produce artifacts, bad enough to fix?
      return m_isInFrustum;
    }

    //----------------------------------------------------------------------------
    bool Model::IsInFrustum(const SpatialBoundingFrustum& frustum) const
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
    void Model::EnablePoseLerp(bool enable)
    {
      m_enableLerp = enable;
    }

    //----------------------------------------------------------------------------
    void Model::DrawMesh(const DirectX::ModelMesh& mesh, bool alpha, std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState)
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
          imatrices->SetMatrices(DirectX::XMLoadFloat4x4(&m_currentPose),
                                 XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().hmdToView[0])),
                                 XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().hmdToView[1])),
                                 XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[0])),
                                 XMLoadFloat4x4(&(m_cameraResources->GetLatestViewProjectionBuffer().projection[1]))
                                );
        }

        DrawMeshPart(*part, setCustomState);
      }
    }

    //----------------------------------------------------------------------------
    void Model::DrawMeshPart(const DirectX::ModelMeshPart& part, std::function<void __cdecl(std::shared_ptr<DirectX::IEffect>)> setCustomState)
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
    void Model::UpdateEffects(_In_ std::function<void __cdecl(DirectX::IEffect*)> setEffect)
    {
      m_model->UpdateEffects(setEffect);
    }

    //----------------------------------------------------------------------------
    void Model::CalculateBounds()
    {
      if (m_model == nullptr)
      {
        std::copy(begin(m_primitive->GetBounds()), end(m_primitive->GetBounds()), begin(m_modelBounds));
        return;
      }
      else if (m_model->meshes.size() == 0)
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