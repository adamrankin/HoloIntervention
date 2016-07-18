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

#include "pch.h"

// Local includes
#include "DirectXHelper.h"
#include "GetDataFromIBuffer.h"
#include "StepTimer.h"
#include "SurfaceMesh.h"

// winrt includes
#include <ppltasks.h>

using namespace DirectX;
using namespace Windows::Perception::Spatial;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Foundation::Numerics;

namespace TrackedUltrasound
{
  namespace Spatial
  {
    //----------------------------------------------------------------------------
    SurfaceMesh::SurfaceMesh()
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      ReleaseDeviceDependentResources();
      m_lastUpdateTime.UniversalTime = 0;
    }

    //----------------------------------------------------------------------------
    SurfaceMesh::~SurfaceMesh()
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      ReleaseDeviceDependentResources();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateSurface( SpatialSurfaceMesh^ surfaceMesh )
    {
      m_surfaceMesh = surfaceMesh;
      m_updateNeeded = true;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::UpdateDeviceBasedResources( ID3D11Device* device )
    {
      std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

      ReleaseDeviceDependentResources();
      CreateDeviceDependentResources( device );
    }

    //----------------------------------------------------------------------------
    // Spatial Mapping surface meshes each have a transform. This transform is updated every frame.
    void SurfaceMesh::UpdateTransform( ID3D11DeviceContext* context, DX::StepTimer const& timer, SpatialCoordinateSystem^ baseCoordinateSystem )
    {
      if ( m_surfaceMesh == nullptr )
      {
        // Not yet ready.
        m_isActive = false;
      }

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
    void SurfaceMesh::CreateDirectXBuffer( ID3D11Device* device, D3D11_BIND_FLAG binding, IBuffer^ buffer, ID3D11Buffer** target )
    {
      auto length = buffer->Length;

      CD3D11_BUFFER_DESC bufferDescription( buffer->Length, binding );
      D3D11_SUBRESOURCE_DATA bufferBytes = { TrackedUltrasound::GetDataFromIBuffer( buffer ), 0, 0 };
      device->CreateBuffer( &bufferDescription, &bufferBytes, target );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateVertexResources( ID3D11Device* device )
    {
      if ( m_surfaceMesh == nullptr )
      {
        // Not yet ready.
        m_isActive = false;
        return;
      }

      m_indexCount = m_surfaceMesh->TriangleIndices->ElementCount;

      if ( m_indexCount < 3 )
      {
        // Not enough indices to draw a triangle.
        m_isActive = false;
        return;
      }

      // Surface mesh resources are created off-thread, so that they don't affect rendering latency.'
      auto taskOptions = Concurrency::task_options();
      auto task = concurrency::create_task( [this, device]()
      {
        // Create new Direct3D device resources for the updated buffers. These will be set aside
        // for now, and then swapped into the active slot next time the render loop is ready to draw.

        // First, we acquire the raw data buffers.
        Windows::Storage::Streams::IBuffer^ positions = m_surfaceMesh->VertexPositions->Data;
        Windows::Storage::Streams::IBuffer^ normals = m_surfaceMesh->VertexNormals->Data;
        Windows::Storage::Streams::IBuffer^ indices = m_surfaceMesh->TriangleIndices->Data;

        // Then, we create Direct3D device buffers with the mesh data provided by HoloLens.
        Microsoft::WRL::ComPtr<ID3D11Buffer> updatedVertexPositions;
        Microsoft::WRL::ComPtr<ID3D11Buffer> updatedVertexNormals;
        Microsoft::WRL::ComPtr<ID3D11Buffer> updatedTriangleIndices;
        CreateDirectXBuffer( device, D3D11_BIND_VERTEX_BUFFER, positions, updatedVertexPositions.GetAddressOf() );
        CreateDirectXBuffer( device, D3D11_BIND_VERTEX_BUFFER, normals, updatedVertexNormals.GetAddressOf() );
        CreateDirectXBuffer( device, D3D11_BIND_INDEX_BUFFER, indices, updatedTriangleIndices.GetAddressOf() );

        // Before sending the new meshes to the renderer, check to ensure that there wasn't a more recent update.
        {
          std::lock_guard<std::mutex> lock( m_meshResourcesMutex );

          auto meshUpdateTime = m_surfaceMesh->SurfaceInfo->UpdateTime;
          if ( meshUpdateTime.UniversalTime > m_lastUpdateTime.UniversalTime )
          {
            // Prepare to swap in the new meshes.
            m_updatedVertexPositions.Swap( updatedVertexPositions );
            m_updatedVertexNormals.Swap( updatedVertexNormals );
            m_updatedTriangleIndices.Swap( updatedTriangleIndices );

            // Cache properties for the buffers we will now use.
            m_vertexStride = m_surfaceMesh->VertexPositions->Stride;
            m_normalStride = m_surfaceMesh->VertexNormals->Stride;
            m_indexFormat = static_cast<DXGI_FORMAT>( m_surfaceMesh->TriangleIndices->Format );

            // Send a signal to the render loop indicating that new resources are available to use.
            m_updateReady = true;
            m_lastUpdateTime = meshUpdateTime;
          }
        }
      } );
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::CreateDeviceDependentResources( ID3D11Device* device )
    {
      CreateVertexResources( device );

      m_loadingComplete = true;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseVertexResources()
    {
      m_vertexPositions.Reset();
      m_vertexNormals.Reset();
      m_triangleIndices.Reset();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::GetUpdatedVertexResources()
    {
      // Swap out the previous vertex position, normal, and index buffers, and replace
      // them with up-to-date buffers.
      m_vertexPositions = m_updatedVertexPositions;
      m_vertexNormals = m_updatedVertexNormals;
      m_triangleIndices = m_updatedTriangleIndices;

      m_updatedVertexPositions.Reset();
      m_updatedVertexNormals.Reset();
      m_updatedTriangleIndices.Reset();
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::ReleaseDeviceDependentResources()
    {
      // Clear out any pending resources.
      GetUpdatedVertexResources();

      // Clear out active resources.
      ReleaseVertexResources();

      m_loadingComplete = false;
    }

    //----------------------------------------------------------------------------
    void SurfaceMesh::SetIsActive( const bool& isActive )
    {
      m_isActive = isActive;
    }

    //----------------------------------------------------------------------------
    const Windows::Foundation::DateTime& SurfaceMesh::GetLastUpdateTime() const
    {
      return m_lastUpdateTime;
    }

    //----------------------------------------------------------------------------
    const float& SurfaceMesh::GetLastActiveTime() const
    {
      return m_lastActiveTime;
    }

    //----------------------------------------------------------------------------
    const bool& SurfaceMesh::GetIsActive() const
    {
      return m_isActive;
    }
  }
}