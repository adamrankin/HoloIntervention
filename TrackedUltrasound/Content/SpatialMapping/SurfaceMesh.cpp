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

#include "pch.h"

// Local includes
#include "DirectXHelper.h"
#include "GetDataFromIBuffer.h"
#include "SpatialShaderStructures.h"
#include "SpatialSurfaceCollection.h"
#include "StepTimer.h"
#include "SurfaceMesh.h"

// winrt includes
#include <ppltasks.h>

// DirectX includes
#include <d3dcompiler.h>

using namespace concurrency;
using namespace DirectX;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Foundation::Numerics;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh( const std::shared_ptr<DX::DeviceResources>& deviceResources )
      : m_deviceResources( deviceResources )
    {
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    SurfaceMesh::~SurfaceMesh()
    {
      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateSurface( SpatialSurfaceMesh^ surfaceMesh )
    {
      m_surfaceMesh = surfaceMesh;
      UpdateDeviceBasedResources();
    }

    //----------------------------------------------------------------------------
    // Spatial Mapping surface meshes each have a transform. This transform is updated every frame.
    void SurfaceMesh::UpdateTransform( DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem )
    {
      if ( m_surfaceMesh == nullptr )
      {
        // Not yet ready.
        m_isActive = false;
      }

      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      // If the surface is active this frame, we need to update its transform.
      XMMATRIX transform;
      if ( m_isActive )
      {
        // The transform is updated relative to a SpatialCoordinateSystem. In the SurfaceMesh class, we
        // expect to be given the same SpatialCoordinateSystem that will be used to generate view
        // matrices, because this class uses the surface mesh for rendering.
        // Other applications could potentially involve using a SpatialCoordinateSystem from a stationary
        // reference frame that is being used for physics simulation, etc.
        auto tryTransform = m_surfaceMesh->CoordinateSystem->TryGetTransformTo( baseCoordinateSystem );
        if ( tryTransform != nullptr )
        {
          // If the transform can be acquired, this spatial mesh is valid right now and
          // we have the information we need to draw it this frame.
          transform = XMLoadFloat4x4( &tryTransform->Value );
          m_lastActiveTime = static_cast<float>( timer.GetTotalSeconds() );
        }
        else
        {
          // If the transform is not acquired, the spatial mesh is not valid right now
          // because its location cannot be correlated to the current space.
          m_isActive = false;
        }
      }

      if ( !m_isActive )
      {
        // If for any reason the surface mesh is not active this frame - whether because
        // it was not included in the observer's collection, or because its transform was
        // not located - we don't have the information we need to update it.
        return;
      }

      // Set up a transform from surface mesh space, to world space.
      XMMATRIX scaleTransform = XMMatrixScalingFromVector( XMLoadFloat3( &m_surfaceMesh->VertexPositionScale ) );
      XMStoreFloat4x4(
        &m_meshToWorldTransform,
        XMMatrixTranspose( scaleTransform * transform )
      );

      // Surface meshes come with normals, which are also transformed from surface mesh space, to world space.
      XMMATRIX normalTransform = transform;
      // Normals are not translated, so we remove the translation component here.
      normalTransform.r[3] = XMVectorSet( 0.f, 0.f, 0.f, XMVectorGetW( normalTransform.r[3] ) );
      XMStoreFloat4x4(
        &m_normalToWorldTransform,
        XMMatrixTranspose( normalTransform )
      );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateDeviceDependentResources()
    {
        if ( m_surfaceMesh == nullptr )
        {
          m_isActive = false;
          return;
        }

        {
          std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
          m_indexCount = m_surfaceMesh->TriangleIndices->ElementCount;

          if ( m_indexCount < 3 )
          {
            m_isActive = false;
            return;
          }
        }

        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

        SpatialSurfaceMeshBuffer^ positions = m_surfaceMesh->VertexPositions;
        SpatialSurfaceMeshBuffer^ indices = m_surfaceMesh->TriangleIndices;

        DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( VertexBufferType ), positions, m_vertexPositionBuffer.GetAddressOf() ) );
        DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( IndexBufferType ), indices, m_indexBuffer.GetAddressOf() ) );
        DX::ThrowIfFailed( CreateStructuredBuffer( sizeof( OutputBufferType ), 1, m_outputBuffer.GetAddressOf() ) );
        DX::ThrowIfFailed( CreateReadbackBuffer( sizeof( OutputBufferType ), 1 ) );

#if defined(_DEBUG) || defined(PROFILE)
        m_vertexPositionBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "MeshBuffer" ) - 1, "MeshBuffer" );
        m_indexBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "IndexBuffer" ) - 1, "IndexBuffer" );
        m_outputBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "OutputBuffer" ) - 1, "OutputBuffer" );
        m_readBackBuffer->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "ReadbackBuffer" ) - 1, "ReadbackBuffer" );
#endif

        DX::ThrowIfFailed( CreateBufferSRV( m_vertexPositionBuffer, positions, m_meshSRV.GetAddressOf() ) );
        DX::ThrowIfFailed( CreateBufferSRV( m_indexBuffer, indices, m_indexSRV.GetAddressOf() ) );
        DX::ThrowIfFailed( CreateBufferUAV( m_outputBuffer, m_outputUAV.GetAddressOf() ) );

#if defined(_DEBUG) || defined(PROFILE)
        m_meshSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Mesh SRV" ) - 1, "Mesh SRV" );
        m_indexSRV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Index SRV" ) - 1, "Index SRV" );
        m_outputUAV->SetPrivateData( WKPDID_D3DDebugObjectName, sizeof( "Output UAV" ) - 1, "Output UAV" );
#endif

        m_lastUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;

        m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseDeviceDependentResources()
    {
      // Clear out active resources.
      m_meshSRV.Reset();
      m_indexSRV.Reset();
      m_outputUAV.Reset();
      m_vertexPositionBuffer.Reset();
      m_indexBuffer.Reset();
      m_outputBuffer.Reset();
      m_readBackBuffer.Reset();

      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    bool SurfaceMesh::TestRayIntersection( ID3D11Device& device,
                                           ID3D11DeviceContext& context,
                                           ID3D11ComputeShader& computeShader,
                                           uint64_t frameNumber,
                                           float3& outHitPosition,
                                           float3& outHitNormal )
    {
      {
        std::lock_guard<std::mutex> lock( m_meshResourcesMutex );
        outHitPosition = float3::zero();
        outHitNormal = float3::zero();

        if ( m_lastFrameNumberComputed != 0 && frameNumber < m_lastFrameNumberComputed + NUMBER_OF_FRAMES_BEFORE_RECOMPUTE )
        {
          // Asked twice in the same frame, return the cached result
          outHitPosition = m_rayIntersectionResultPosition;
          outHitNormal = m_rayIntersectionResultNormal;
          return m_hasLastComputedHit;
        }

        // TODO : implement pre-check using OBB
        //m_surfaceMesh->SurfaceInfo->TryGetBounds()

        ID3D11ShaderResourceView* aRViews[2] = { m_meshSRV.Get(), m_indexSRV.Get() };
        // Send in the number of triangles as the number of thread groups to dispatch
        // triangleCount = m_indexCount/3
        RunComputeShader( context, computeShader, 2, aRViews, m_outputUAV.Get(), m_indexCount / 3, 1, 1 );

        context.CopyResource( m_readBackBuffer.Get(), m_outputBuffer.Get() );
      }

      D3D11_MAPPED_SUBRESOURCE MappedResource;
      OutputBufferType* result;
      context.Map( m_readBackBuffer.Get(), 0, D3D11_MAP_READ, 0, &MappedResource );

      result = ( OutputBufferType* )MappedResource.pData;

      context.Unmap( m_readBackBuffer.Get(), 0 );

      m_lastFrameNumberComputed = frameNumber;
      outHitPosition = m_rayIntersectionResultPosition = float3( result->intersectionPoint[0], result->intersectionPoint[1], result->intersectionPoint[2] );
      outHitNormal = m_rayIntersectionResultNormal = float3( result->intersectionNormal[0], result->intersectionNormal[1], result->intersectionNormal[2] );

      if ( result->intersectionPoint[0] != 0.0f || result->intersectionPoint[1] != 0.0f || result->intersectionPoint[2] != 0.0f ||
           result->intersectionNormal[0] != 0.0f || result->intersectionNormal[1] != 0.0f || result->intersectionNormal[2] != 0.0f )
      {
        m_hasLastComputedHit = true;
        return true;
      }

      m_hasLastComputedHit = false;
      return false;
    }

    //----------------------------------------------------------------------------
    const bool& SurfaceMesh::GetIsActive() const
    {
      return m_isActive;
    }

    //----------------------------------------------------------------------------
    const float& SurfaceMesh::GetLastActiveTime() const
    {
      return m_lastActiveTime;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::DateTime& SurfaceMesh::GetLastUpdateTime() const
    {
      return m_lastUpdateTime;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetIsActive( const bool& isActive )
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetRayConstants( ID3D11DeviceContext& context,
                                       ID3D11Buffer* constantBuffer,
                                       const float3 rayOrigin,
                                       const float3 rayDirection )
    {
      ConstantBuffer cb;
      cb.rayOrigin[0] = rayOrigin.x;
      cb.rayOrigin[1] = rayOrigin.y;
      cb.rayOrigin[2] = rayOrigin.z;
      cb.rayDirection[0] = rayDirection.x;
      cb.rayDirection[1] = rayDirection.y;
      cb.rayDirection[2] = rayDirection.z;
      cb.meshToWorld = m_meshToWorldTransform;

      context.UpdateSubresource( constantBuffer, 0, nullptr, &cb, 0, 0 );
      context.CSSetConstantBuffers( 0, 1, &constantBuffer );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateDeviceBasedResources()
    {
      ReleaseDeviceDependentResources();
      CreateDeviceDependentResources();
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( uint32 uStructureSize, SpatialSurfaceMeshBuffer^ buffer, ID3D11Buffer** target )
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = buffer->Data->Length;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uStructureSize;

      D3D11_SUBRESOURCE_DATA bufferBytes = { TrackedUltrasound::GetDataFromIBuffer( buffer->Data ), 0, 0 };
      return m_deviceResources->GetD3DDevice()->CreateBuffer( &desc, &bufferBytes, target );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateStructuredBuffer( uint32 uElementSize, uint32 uCount, ID3D11Buffer** target )
    {
      D3D11_BUFFER_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
      desc.ByteWidth = uElementSize * uCount;
      desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
      desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer( &desc, nullptr, target );
    }

    //----------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateReadbackBuffer( uint32 uElementSize, uint32 uCount )
    {
      D3D11_BUFFER_DESC readback_buffer_desc;
      ZeroMemory( &readback_buffer_desc, sizeof( readback_buffer_desc ) );
      readback_buffer_desc.ByteWidth = uElementSize * uCount;
      readback_buffer_desc.Usage = D3D11_USAGE_STAGING;
      readback_buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      readback_buffer_desc.StructureByteStride = uElementSize;

      return m_deviceResources->GetD3DDevice()->CreateBuffer( &readback_buffer_desc, nullptr, &m_readBackBuffer );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferSRV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                                          SpatialSurfaceMeshBuffer^ buffer,
                                          ID3D11ShaderResourceView** ppSRVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer->GetDesc( &descBuf );

      D3D11_SHADER_RESOURCE_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
      desc.BufferEx.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.BufferEx.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateShaderResourceView( computeShaderBuffer.Get(), &desc, ppSRVOut );
    }

    //--------------------------------------------------------------------------------------
    HRESULT SurfaceMesh::CreateBufferUAV( ComPtr<ID3D11Buffer> computeShaderBuffer,
                                          ID3D11UnorderedAccessView** ppUAVOut )
    {
      D3D11_BUFFER_DESC descBuf;
      ZeroMemory( &descBuf, sizeof( descBuf ) );
      computeShaderBuffer->GetDesc( &descBuf );

      D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
      ZeroMemory( &desc, sizeof( desc ) );
      desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
      desc.Buffer.FirstElement = 0;
      desc.Format = DXGI_FORMAT_UNKNOWN;
      desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;

      return m_deviceResources->GetD3DDevice()->CreateUnorderedAccessView( computeShaderBuffer.Get(), &desc, ppUAVOut );
    }

    //--------------------------------------------------------------------------------------
    void SurfaceMesh::RunComputeShader( ID3D11DeviceContext& context,
                                        ID3D11ComputeShader& computeShader,
                                        uint32 nNumViews, ID3D11ShaderResourceView** pShaderResourceViews,
                                        ID3D11UnorderedAccessView* pUnorderedAccessView,
                                        uint32 xThreadGroups,
                                        uint32 yThreadGroups,
                                        uint32 zThreadGroups )
    {
      OutputBufferType output;
      context.UpdateSubresource( m_outputBuffer.Get(), 0, nullptr, &output, 0, 0 );

      // Set the shaders resources
      context.CSSetShaderResources( 0, nNumViews, pShaderResourceViews );
      context.CSSetUnorderedAccessViews( 0, 1, &pUnorderedAccessView, nullptr );

      // The total number of thread groups creates is x*y*z, the number of threads in a thread group is determined by numthreads(i,j,k) in the shader code
      //context.Dispatch( xThreadGroups, yThreadGroups, zThreadGroups );

      ID3D11UnorderedAccessView* ppUAViewnullptr[1] = { nullptr };
      context.CSSetUnorderedAccessViews( 0, 1, ppUAViewnullptr, nullptr );

      ID3D11ShaderResourceView* ppSRVnullptr[2] = { nullptr, nullptr };
      context.CSSetShaderResources( 0, 2, ppSRVnullptr );

      ID3D11Buffer* ppCBnullptr[1] = { nullptr };
      context.CSSetConstantBuffers( 0, 1, ppCBnullptr );
    }
  }
}