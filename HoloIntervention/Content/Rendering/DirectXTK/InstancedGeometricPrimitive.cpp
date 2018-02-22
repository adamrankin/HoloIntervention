//--------------------------------------------------------------------------------------
// File: InstancedGeometricPrimitive.cpp
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// http://go.microsoft.com/fwlink/?LinkId=248929
//--------------------------------------------------------------------------------------

#include "pch.h"
#include "CommonStates.h"
#include "DirectXHelpers.h"
#include "Geometry.h"
#include "InstancedEffects.h"
#include "InstancedGeometricPrimitive.h"
#include "SharedResourcePool.h"

// STL includes
#include <array>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
  // Helper for creating a D3D vertex or index buffer.
  template<typename T>
  static void CreateBuffer(_In_ ID3D11Device* device, T const& data, D3D11_BIND_FLAG bindFlags, _Outptr_ ID3D11Buffer** pBuffer)
  {
    assert(pBuffer != 0);

    D3D11_BUFFER_DESC bufferDesc = {};

    bufferDesc.ByteWidth = (UINT)data.size() * sizeof(T::value_type);
    bufferDesc.BindFlags = bindFlags;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;

    D3D11_SUBRESOURCE_DATA dataDesc = {};

    dataDesc.pSysMem = data.data();

    ThrowIfFailed(
      device->CreateBuffer(&bufferDesc, &dataDesc, pBuffer)
    );

    _Analysis_assume_(*pBuffer != 0);

    SetDebugObjectName(*pBuffer, "DirectXTK:InstancedGeometricPrimitive");
  }


  // Helper for creating a D3D input layout.
  void CreateInputLayout(_In_ ID3D11Device* device, IEffect* effect, _Outptr_ ID3D11InputLayout** pInputLayout)
  {
    assert(pInputLayout != 0);

    void const* shaderByteCode;
    size_t byteCodeLength;

    effect->GetVertexShaderBytecode(&shaderByteCode, &byteCodeLength);

    ThrowIfFailed(
      device->CreateInputLayout(VertexPositionNormalTexture::InputElements,
                                VertexPositionNormalTexture::InputElementCount,
                                shaderByteCode, byteCodeLength,
                                pInputLayout)
    );

    _Analysis_assume_(*pInputLayout != 0);

    SetDebugObjectName(*pInputLayout, "DirectXTK:InstancedGeometricPrimitive");
  }
}

// Internal InstancedGeometricPrimitive implementation class.
class InstancedGeometricPrimitive::Impl
{
public:
  void Initialize(_In_ ID3D11DeviceContext* deviceContext, const VertexCollection& vertices, const IndexCollection& indices);

  const std::array<float, 6>& XM_CALLCONV GetBounds() const;

  void XM_CALLCONV Draw(FXMMATRIX world, CXMMATRIX leftView, CXMMATRIX rightView, CXMMATRIX leftProjection, CXMMATRIX rightProjection, CXMVECTOR color, _In_opt_ ID3D11ShaderResourceView* texture, bool wireframe, _In_opt_ std::function<void()> setCustomState);

  void Draw(_In_ IEffect* effect, _In_ ID3D11InputLayout* inputLayout, bool alpha, bool wireframe, _In_opt_ std::function<void()> setCustomState);

  void CreateInputLayout(_In_ IEffect* effect, _Outptr_ ID3D11InputLayout** inputLayout);

protected:
  void ComputeBounds(const VertexCollection& vertices);

private:
  ComPtr<ID3D11Buffer> mVertexBuffer;
  ComPtr<ID3D11Buffer> mIndexBuffer;

  std::array<float, 6> mBounds;

  UINT mIndexCount;

  // Only one of these helpers is allocated per D3D device context, even if there are multiple InstancedGeometricPrimitive instances.
  class SharedResources
  {
  public:
    SharedResources(_In_ ID3D11DeviceContext* deviceContext);

    void PrepareForRendering(bool alpha, bool wireframe);

    ComPtr<ID3D11DeviceContext> deviceContext;
    std::unique_ptr<InstancedBasicEffect> effect;

    ComPtr<ID3D11InputLayout> inputLayoutTextured;
    ComPtr<ID3D11InputLayout> inputLayoutUntextured;

    std::unique_ptr<CommonStates> stateObjects;
  };


  // Per-device-context data.
  std::shared_ptr<SharedResources> mResources;

  static SharedResourcePool<ID3D11DeviceContext*, SharedResources> sharedResourcesPool;
};

// Global pool of per-device-context InstancedGeometricPrimitive resources.
SharedResourcePool<ID3D11DeviceContext*, InstancedGeometricPrimitive::Impl::SharedResources> InstancedGeometricPrimitive::Impl::sharedResourcesPool;

// Per-device-context constructor.
InstancedGeometricPrimitive::Impl::SharedResources::SharedResources(_In_ ID3D11DeviceContext* deviceContext)
  : deviceContext(deviceContext)
{
  ComPtr<ID3D11Device> device;
  deviceContext->GetDevice(&device);

  // Create the BasicEffect.
  effect = std::make_unique<InstancedBasicEffect>(device.Get());

  effect->EnableDefaultLighting();

  // Create state objects.
  stateObjects = std::make_unique<CommonStates>(device.Get());

  // Create input layouts.
  effect->SetTextureEnabled(true);
  ::CreateInputLayout(device.Get(), effect.get(), &inputLayoutTextured);

  effect->SetTextureEnabled(false);
  ::CreateInputLayout(device.Get(), effect.get(), &inputLayoutUntextured);
}

// Sets up D3D device state ready for drawing a primitive.
void InstancedGeometricPrimitive::Impl::SharedResources::PrepareForRendering(bool alpha, bool wireframe)
{
  // Set the blend and depth stencil state.
  ID3D11BlendState* blendState;
  ID3D11DepthStencilState* depthStencilState;

  if (alpha)
  {
    // Alpha blended rendering.
    blendState = stateObjects->AlphaBlend();
    depthStencilState = stateObjects->DepthRead();
  }
  else
  {
    // Opaque rendering.
    blendState = stateObjects->Opaque();
    depthStencilState = stateObjects->DepthDefault();
  }

  deviceContext->OMSetBlendState(blendState, nullptr, 0xFFFFFFFF);
  deviceContext->OMSetDepthStencilState(depthStencilState, 0);

  // Set the rasterizer state.
  if (wireframe)
  { deviceContext->RSSetState(stateObjects->Wireframe()); }
  else
  { deviceContext->RSSetState(stateObjects->CullCounterClockwise()); }

  ID3D11SamplerState* samplerState = stateObjects->LinearWrap();

  deviceContext->PSSetSamplers(0, 1, &samplerState);
}

// Initializes a geometric primitive instance that will draw the specified vertex and index data.
_Use_decl_annotations_
void InstancedGeometricPrimitive::Impl::Initialize(ID3D11DeviceContext* deviceContext, const VertexCollection& vertices, const IndexCollection& indices)
{
  if (vertices.size() >= USHRT_MAX)
  {
    throw std::exception("Too many vertices for 16-bit index buffer");
  }

  mResources = sharedResourcesPool.DemandCreate(deviceContext);

  ComPtr<ID3D11Device> device;
  deviceContext->GetDevice(&device);

  CreateBuffer(device.Get(), vertices, D3D11_BIND_VERTEX_BUFFER, &mVertexBuffer);
  CreateBuffer(device.Get(), indices, D3D11_BIND_INDEX_BUFFER, &mIndexBuffer);

  ComputeBounds(vertices);

  mIndexCount = static_cast<UINT>(indices.size());
}

// Gets the bounding extents of the primitive
_Use_decl_annotations_
const std::array<float, 6>& XM_CALLCONV InstancedGeometricPrimitive::Impl::GetBounds() const
{
  return mBounds;
}

// Draws the primitive.
_Use_decl_annotations_
void XM_CALLCONV InstancedGeometricPrimitive::Impl::Draw(FXMMATRIX world,
    CXMMATRIX leftView,
    CXMMATRIX rightView,
    CXMMATRIX leftProjection,
    CXMMATRIX rightProjection,
    CXMVECTOR color,
    ID3D11ShaderResourceView* texture,
    bool wireframe,
    std::function<void()> setCustomState)
{
  assert(mResources != nullptr);
  InstancedBasicEffect* effect = mResources->effect.get();
  assert(effect != nullptr);

  ID3D11InputLayout* inputLayout;
  if (texture)
  {
    effect->SetTextureEnabled(true);
    effect->SetTexture(texture);

    inputLayout = mResources->inputLayoutTextured.Get();
  }
  else
  {
    effect->SetTextureEnabled(false);

    inputLayout = mResources->inputLayoutUntextured.Get();
  }

  // Set effect parameters.
  effect->SetMatrices(world, leftView, rightView, leftProjection, rightProjection);

  effect->SetColorAndAlpha(color);

  float alpha = XMVectorGetW(color);
  Draw(effect, inputLayout, (alpha < 1.f), wireframe, setCustomState);
}

// Draw the primitive using a custom effect.
_Use_decl_annotations_
void InstancedGeometricPrimitive::Impl::Draw(IEffect* effect, ID3D11InputLayout* inputLayout, bool alpha, bool wireframe, std::function<void()> setCustomState)
{
  assert(mResources != 0);
  auto deviceContext = mResources->deviceContext.Get();
  assert(deviceContext != 0);

  // Set state objects.
  mResources->PrepareForRendering(alpha, wireframe);

  // Set input layout.
  assert(inputLayout != 0);
  deviceContext->IASetInputLayout(inputLayout);

  // Activate our shaders, constant buffers, texture, etc.
  assert(effect != 0);
  effect->Apply(deviceContext);

  // Set the vertex and index buffer.
  auto vertexBuffer = mVertexBuffer.Get();
  UINT vertexStride = sizeof(VertexPositionNormalTexture);
  UINT vertexOffset = 0;

  deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);

  deviceContext->IASetIndexBuffer(mIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);

  // Hook lets the caller replace our shaders or state settings with whatever else they see fit.
  if (setCustomState)
  {
    setCustomState();
  }

  // Draw the primitive.
  deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  deviceContext->DrawIndexedInstanced(mIndexCount, 2, 0, 0, 0);
}

// Create input layout for drawing with a custom effect.
_Use_decl_annotations_
void InstancedGeometricPrimitive::Impl::CreateInputLayout(IEffect* effect, ID3D11InputLayout** inputLayout)
{
  assert(effect != 0);
  assert(inputLayout != 0);

  assert(mResources != 0);
  auto deviceContext = mResources->deviceContext.Get();
  assert(deviceContext != 0);

  ComPtr<ID3D11Device> device;
  deviceContext->GetDevice(&device);

  ::CreateInputLayout(device.Get(), effect, inputLayout);
}

// Calculate the bounds of the primitive
_Use_decl_annotations_
void InstancedGeometricPrimitive::Impl::ComputeBounds(const VertexCollection& vertices)
{
  if (vertices.size() == 0)
  {
    mBounds[0] = mBounds[1] = mBounds[2] = mBounds[3] = mBounds[4] = mBounds[5] = 0.f;
    return;
  }

  mBounds[1] = vertices[0].position.x;
  mBounds[0] = vertices[0].position.x;
  mBounds[3] = vertices[0].position.y;
  mBounds[2] = vertices[0].position.y;
  mBounds[5] = vertices[0].position.z;
  mBounds[4] = vertices[0].position.z;
  for (auto& entry : vertices)
  {
    mBounds[1] = std::fmax(entry.position.x, mBounds[1]);
    mBounds[0] = std::fmin(entry.position.x, mBounds[0]);
    mBounds[3] = std::fmax(entry.position.y, mBounds[3]);
    mBounds[2] = std::fmin(entry.position.y, mBounds[2]);
    mBounds[5] = std::fmax(entry.position.z, mBounds[5]);
    mBounds[4] = std::fmin(entry.position.z, mBounds[4]);
  }
}

//--------------------------------------------------------------------------------------
// InstancedGeometricPrimitive
//--------------------------------------------------------------------------------------
InstancedGeometricPrimitive::InstancedGeometricPrimitive()
  : pImpl(new Impl())
{
}

// Destructor.
InstancedGeometricPrimitive::~InstancedGeometricPrimitive()
{
}


// Public entrypoints.
const std::array<float, 6>& XM_CALLCONV DirectX::InstancedGeometricPrimitive::GetBounds() const
{
  return pImpl->GetBounds();
}

_Use_decl_annotations_
void XM_CALLCONV InstancedGeometricPrimitive::Draw(FXMMATRIX world, CXMMATRIX leftView, CXMMATRIX rightView, CXMMATRIX leftProjection, CXMMATRIX rightProjection, CXMVECTOR color, ID3D11ShaderResourceView* texture, bool wireframe, std::function<void()> setCustomState)
{
  pImpl->Draw(world, leftView, rightView, leftProjection, rightProjection, color, texture, wireframe, setCustomState);
}

_Use_decl_annotations_
void InstancedGeometricPrimitive::Draw(IEffect* effect, ID3D11InputLayout* inputLayout, bool alpha, bool wireframe, std::function<void()> setCustomState)
{
  pImpl->Draw(effect, inputLayout, alpha, wireframe, setCustomState);
}

_Use_decl_annotations_
void InstancedGeometricPrimitive::CreateInputLayout(IEffect* effect, ID3D11InputLayout** inputLayout)
{
  pImpl->CreateInputLayout(effect, inputLayout);
}

//--------------------------------------------------------------------------------------
// Cube (aka a Hexahedron) or Box
//--------------------------------------------------------------------------------------

// Creates a cube primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateCube(_In_ ID3D11DeviceContext* deviceContext, float size, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeBox(vertices, indices, XMFLOAT3(size, size, size), rhcoords, false);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateCube(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, bool rhcoords)
{
  ComputeBox(vertices, indices, XMFLOAT3(size, size, size), rhcoords, false);
}


// Creates a box primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateBox(_In_ ID3D11DeviceContext* deviceContext, const XMFLOAT3& size, bool rhcoords, bool invertn)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeBox(vertices, indices, size, rhcoords, invertn);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateBox(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, const XMFLOAT3& size, bool rhcoords, bool invertn)
{
  ComputeBox(vertices, indices, size, rhcoords, invertn);
}


//--------------------------------------------------------------------------------------
// Sphere
//--------------------------------------------------------------------------------------

// Creates a sphere primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateSphere(_In_ ID3D11DeviceContext* deviceContext, float diameter, size_t tessellation, bool rhcoords, bool invertn)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeSphere(vertices, indices, diameter, tessellation, rhcoords, invertn);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateSphere(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float diameter, size_t tessellation, bool rhcoords, bool invertn)
{
  ComputeSphere(vertices, indices, diameter, tessellation, rhcoords, invertn);
}


//--------------------------------------------------------------------------------------
// Geodesic sphere
//--------------------------------------------------------------------------------------

// Creates a geosphere primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateGeoSphere(_In_ ID3D11DeviceContext* deviceContext, float diameter, size_t tessellation, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeGeoSphere(vertices, indices, diameter, tessellation, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateGeoSphere(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float diameter, size_t tessellation, bool rhcoords)
{
  ComputeGeoSphere(vertices, indices, diameter, tessellation, rhcoords);
}


//--------------------------------------------------------------------------------------
// Cylinder / Cone
//--------------------------------------------------------------------------------------

// Creates a cylinder primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateCylinder(_In_ ID3D11DeviceContext* deviceContext, float height, float diameter, size_t tessellation, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeCylinder(vertices, indices, height, diameter, tessellation, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateCylinder(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float height, float diameter, size_t tessellation, bool rhcoords)
{
  ComputeCylinder(vertices, indices, height, diameter, tessellation, rhcoords);
}


// Creates a cone primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateCone(_In_ ID3D11DeviceContext* deviceContext, float diameter, float height, size_t tessellation, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeCone(vertices, indices, diameter, height, tessellation, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateCone(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float diameter, float height, size_t tessellation, bool rhcoords)
{
  ComputeCone(vertices, indices, diameter, height, tessellation, rhcoords);
}


//--------------------------------------------------------------------------------------
// Torus
//--------------------------------------------------------------------------------------

// Creates a torus primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateTorus(_In_ ID3D11DeviceContext* deviceContext, float diameter, float thickness, size_t tessellation, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeTorus(vertices, indices, diameter, thickness, tessellation, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateTorus(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float diameter, float thickness, size_t tessellation, bool rhcoords)
{
  ComputeTorus(vertices, indices, diameter, thickness, tessellation, rhcoords);
}


//--------------------------------------------------------------------------------------
// Tetrahedron
//--------------------------------------------------------------------------------------

std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateTetrahedron(_In_ ID3D11DeviceContext* deviceContext, float size, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeTetrahedron(vertices, indices, size, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateTetrahedron(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, bool rhcoords)
{
  ComputeTetrahedron(vertices, indices, size, rhcoords);
}


//--------------------------------------------------------------------------------------
// Octahedron
//--------------------------------------------------------------------------------------
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateOctahedron(_In_ ID3D11DeviceContext* deviceContext, float size, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeOctahedron(vertices, indices, size, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateOctahedron(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, bool rhcoords)
{
  ComputeOctahedron(vertices, indices, size, rhcoords);
}


//--------------------------------------------------------------------------------------
// Dodecahedron
//--------------------------------------------------------------------------------------

std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateDodecahedron(_In_ ID3D11DeviceContext* deviceContext, float size, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeDodecahedron(vertices, indices, size, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateDodecahedron(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, bool rhcoords)
{
  ComputeDodecahedron(vertices, indices, size, rhcoords);
}


//--------------------------------------------------------------------------------------
// Icosahedron
//--------------------------------------------------------------------------------------

std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateIcosahedron(_In_ ID3D11DeviceContext* deviceContext, float size, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeIcosahedron(vertices, indices, size, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateIcosahedron(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, bool rhcoords)
{
  ComputeIcosahedron(vertices, indices, size, rhcoords);
}


//--------------------------------------------------------------------------------------
// Teapot
//--------------------------------------------------------------------------------------

// Creates a teapot primitive.
std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateTeapot(_In_ ID3D11DeviceContext* deviceContext, float size, size_t tessellation, bool rhcoords)
{
  VertexCollection vertices;
  IndexCollection indices;
  ComputeTeapot(vertices, indices, size, tessellation, rhcoords);

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}

void InstancedGeometricPrimitive::CreateTeapot(std::vector<VertexPositionNormalTexture>& vertices, std::vector<uint16_t>& indices, float size, size_t tessellation, bool rhcoords)
{
  ComputeTeapot(vertices, indices, size, tessellation, rhcoords);
}


//--------------------------------------------------------------------------------------
// Custom
//--------------------------------------------------------------------------------------

std::unique_ptr<InstancedGeometricPrimitive> InstancedGeometricPrimitive::CreateCustom(_In_ ID3D11DeviceContext* deviceContext, const std::vector<VertexPositionNormalTexture>& vertices, const std::vector<uint16_t>& indices)
{
  // Extra validation
  if (vertices.empty() || indices.empty())
  { throw std::exception("Requires both vertices and indices"); }

  if (indices.size() % 3)
  { throw std::exception("Expected triangular faces"); }

  size_t nVerts = vertices.size();
  if (nVerts >= USHRT_MAX)
  { throw std::exception("Too many vertices for 16-bit index buffer"); }

  for (auto it = indices.cbegin(); it != indices.cend(); ++it)
  {
    if (*it >= nVerts)
    {
      throw std::exception("Index not in vertices list");
    }
  }

  // Create the primitive object.
  std::unique_ptr<InstancedGeometricPrimitive> primitive(new InstancedGeometricPrimitive());

  primitive->pImpl->Initialize(deviceContext, vertices, indices);

  return primitive;
}