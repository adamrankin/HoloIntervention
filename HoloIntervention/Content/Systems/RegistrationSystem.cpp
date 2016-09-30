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
#include "AppView.h"
#include "Common.h"
#include "RegistrationSystem.h"

// Rendering includes
#include "ModelRenderer.h"
#include "ModelEntry.h"

// Spatial includes
#include "SurfaceMesh.h"

// System includes
#include "SpatialSystem.h"
#include "NotificationSystem.h"

// Network includes
#include "IGTLinkIF.h"

// std includes
#include <sstream>

// DirectXTex includes
#include <DirectXTex.h>

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Networking;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace System
  {
    Platform::String^ RegistrationSystem::ANCHOR_NAME = ref new Platform::String( L"Registration" );
    const std::wstring RegistrationSystem::ANCHOR_MODEL_FILENAME = L"Assets/Models/anchor.cmo";

    //----------------------------------------------------------------------------
    RegistrationSystem::RegistrationSystem( const std::shared_ptr<DX::DeviceResources>& deviceResources, DX::StepTimer& stepTimer )
      : m_deviceResources( deviceResources )
      , m_stepTimer( stepTimer )
    {
      m_regAnchorModelId = HoloIntervention::instance()->GetModelRenderer().AddModel( ANCHOR_MODEL_FILENAME );
      if ( m_regAnchorModelId != Rendering::INVALID_MODEL_ENTRY )
      {
        m_regAnchorModel = HoloIntervention::instance()->GetModelRenderer().GetModel( m_regAnchorModelId );
      }
      if ( m_regAnchorModel == nullptr )
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Unable to retrieve anchor model." );
        return;
      }
      m_regAnchorModel->SetVisible( false );

      create_task( Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync( L"Assets\\Data\\tool_configuration.xml" ) ).then( [this]( task<StorageFile^> previousTask )
      {
        StorageFile^ file = nullptr;
        try
        {
          file = previousTask.get();
        }
        catch ( Platform::Exception^ e )
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Unable to locate tool system configuration file." );
        }

        XmlDocument^ doc = ref new XmlDocument();
        create_task( doc->LoadFromFileAsync( file ) ).then( [this]( task<XmlDocument^> previousTask )
        {
          XmlDocument^ doc = nullptr;
          try
          {
            doc = previousTask.get();
          }
          catch ( Platform::Exception^ e )
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Tool system configuration file did not contain valid XML." );
          }

          try
          {
            m_transformRepository->ReadConfiguration( doc );
          }
          catch ( Platform::Exception^ e )
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Invalid layout in coordinate definitions configuration area." );
          }
        } );
      } );
    }

    //----------------------------------------------------------------------------
    RegistrationSystem::~RegistrationSystem()
    {
      m_regAnchorModel = nullptr;
      m_regAnchorModelId = 0;
      m_tokenSource.cancel();
      if ( m_receiverTask != nullptr )
      {
        m_receiverTask->wait();
      }
      m_connected = false;
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::Update( SpatialCoordinateSystem^ coordinateSystem, SpatialPointerPose^ headPose )
    {
      // Anchor placement logic
      if ( m_regAnchorRequested )
      {
        if ( HoloIntervention::instance()->GetSpatialSystem().DropAnchorAtIntersectionHit( ANCHOR_NAME, coordinateSystem, headPose ) )
        {
          m_regAnchorRequested = false;
          if ( m_regAnchorModel != nullptr )
          {
            m_regAnchorModel->SetVisible( true );
          }

          m_spatialMesh = HoloIntervention::instance()->GetSpatialSystem().GetLastHitMesh();

          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Anchor created." );
        }
      }

      // Anchor position update logic
      if ( HoloIntervention::instance()->GetSpatialSystem().HasAnchor( L"Registration" ) )
      {
        auto transformContainer = HoloIntervention::instance()->GetSpatialSystem().GetAnchor( ANCHOR_NAME )->CoordinateSystem->TryGetTransformTo( coordinateSystem );
        if ( transformContainer != nullptr )
        {
          float4x4 anchorToWorld = transformContainer->Value;

          // Coordinate system has orientation and position
          m_regAnchorModel->SetWorld( anchorToWorld );
        }
      }

      // Point collection logic
      if ( m_collectingPoints && HoloIntervention::instance()->GetIGTLink().IsConnected() )
      {
        if ( HoloIntervention::instance()->GetIGTLink().GetLatestTrackedFrame( m_trackedFrame, &m_latestTimestamp ) )
        {
          m_transformRepository->SetTransforms( m_trackedFrame );
          bool isValid;
          float4x4 stylusTipToReference;
          try
          {
            stylusTipToReference = m_transformRepository->GetTransform( m_stylusTipToReferenceName, &isValid );
            // Put into column order so that win numerics functions work as expected
            stylusTipToReference = transpose( stylusTipToReference );
            if ( isValid )
            {
              float3 point = translation( stylusTipToReference );
              m_points.push_back( point );
            }
          }
          catch ( Platform::Exception^ e )
          {
            OutputDebugStringW( e->Message->Data() );
            //HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Unable to add point." );
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::LoadAppStateAsync()
    {
      return create_task( [ = ]()
      {
        if ( HoloIntervention::instance()->GetSpatialSystem().HasAnchor( ANCHOR_NAME ) )
        {
          m_regAnchorModel->SetVisible( true );
        }
      } );
    }

    //----------------------------------------------------------------------------
    void RegistrationSystem::RegisterVoiceCallbacks( HoloIntervention::Sound::VoiceInputCallbackMap& callbackMap, void* userArg )
    {
      callbackMap[L"start collecting points"] = [this]( SpeechRecognitionResult ^ result )
      {
        if ( HoloIntervention::instance()->GetIGTLink().IsConnected() )
        {
          m_points.clear();
          m_latestTimestamp = 0;
          m_collectingPoints = true;
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Collecting points..." );
        }
        else
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Not connected!" );
        }
      };

      callbackMap[L"end collecting points"] = [this]( SpeechRecognitionResult ^ result )
      {
        m_collectingPoints = false;
        if ( m_points.size() == 0 )
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"No points collected." );
          return;
        }
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Collecting finished." );

        SendRegistrationDataAsync().then( [this]( bool result )
        {
          if ( result )
          {
            std::stringstream ss;
            ss << m_points.size() << " points collected. Computing registration...";
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( ss.str() );
          }
        } );
      };

      callbackMap[L"drop anchor"] = [this]( SpeechRecognitionResult ^ result )
      {
        m_regAnchorRequested = true;
      };

      callbackMap[L"remove anchor"] = [this]( SpeechRecognitionResult ^ result )
      {
        if ( m_regAnchorModel )
        {
          m_regAnchorModel->SetVisible( false );
        }
        if ( HoloIntervention::instance()->GetSpatialSystem().RemoveAnchor( ANCHOR_NAME ) == 1 )
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Anchor \"" + ANCHOR_NAME + "\" removed." );
        }
      };
    }

    //----------------------------------------------------------------------------
    task<bool> RegistrationSystem::SendRegistrationDataAsync()
    {
      return create_task( [ = ]() -> bool
      {
        auto hostname = ref new HostName( ref new Platform::String( HoloIntervention::instance()->GetIGTLink().GetHostname().c_str() ) );

        if ( !m_connected )
        {
          auto connectTask = create_task( m_networkPCLSocket->ConnectAsync( hostname, ref new Platform::String( L"24012" ) ) );

          try
          {
            connectTask.wait();
          }
          catch ( Platform::Exception^ e )
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Unable to connect to NetworkPCL." );
            return false;
          }

          m_connected = true;
        }

        DataWriter^ writer = ref new DataWriter( m_networkPCLSocket->OutputStream );
        SpatialSurfaceMesh^ mesh = m_spatialMesh->GetSurfaceMesh();
        XMFLOAT4X4& meshToWorld = m_spatialMesh->GetMeshToWorldTransform();

        // Calculate the body size
        unsigned int bodySize = mesh->TriangleIndices->ElementCount * sizeof( float ) * 3 + m_points.size() * sizeof( float ) * 3;

        // First, write header details to the stream
        writer->WriteUInt16( NetworkPCL::NetworkPCL_POINT_DATA );
        writer->WriteUInt32( 0 ); // no additional header data
        writer->WriteUInt32( bodySize );
        writer->WriteUInt32( mesh->TriangleIndices->ElementCount );
        writer->WriteUInt32( m_points.size() );

        // Convert the mesh data to a stream of vertices, de-indexed
        float* verticesComponents = GetDataFromIBuffer<float>( mesh->VertexPositions->Data );
        std::vector<XMFLOAT3> vertices;
        for ( unsigned int i = 0; i < mesh->VertexPositions->ElementCount; ++i )
        {
          XMFLOAT3 vertex( verticesComponents[0], verticesComponents[1], verticesComponents[2] );

          // Transform it into world coordinates
          XMStoreFloat3( &vertex, XMVector3Transform( XMLoadFloat3( &vertex ), XMLoadFloat4x4( &meshToWorld ) ) );

          // Store it
          vertices.push_back( vertex );

          verticesComponents += 3;
          if ( HasAlpha( ( DXGI_FORMAT )mesh->VertexPositions->Format ) )
          {
            // Skip alpha value
            verticesComponents++;
          }
        }

        uint32* indicies = GetDataFromIBuffer<uint32>( mesh->TriangleIndices->Data );
        std::vector<uint32> indiciesVector;
        for ( unsigned int i = 0; i < mesh->TriangleIndices->ElementCount; ++i )
        {
          indiciesVector.push_back( *indicies );
          indicies++;
        }

        for ( unsigned int i = 0; i < indiciesVector.size(); i++ )
        {
          writer->WriteSingle( vertices[indiciesVector[i]].x );
          writer->WriteSingle( vertices[indiciesVector[i]].y );
          writer->WriteSingle( vertices[indiciesVector[i]].z );
        }

        for ( auto& point : m_points )
        {
          writer->WriteSingle( point.x );
          writer->WriteSingle( point.y );
          writer->WriteSingle( point.z );
        }

        create_task( writer->StoreAsync() ).then( [ = ]( task<uint32> writeTask )
        {
          uint32 bytesWritten;
          try
          {
            bytesWritten = writeTask.get();
          }
          catch ( Platform::Exception^ exception )
          {
            std::wstring message( exception->Message->Data() );
            std::string messageStr( message.begin(), message.end() );
            throw std::exception( messageStr.c_str() );
          }

          if ( bytesWritten != bodySize + sizeof( NetworkPCL::PCLMessageHeader ) )
          {
            throw std::exception( "Entire message couldn't be sent." );
          }

          // Start the asynchronous data receiver thread
          m_registrationResultReceived = false;
          m_receiverTask = &DataReceiverAsync();

          cancellation_token_source cts;
          auto waitTask = cancel_after_timeout( WaitForRegistrationResultAsync(), cts, 10000 );

          try
          {
            waitTask.wait();
          }
          catch ( const task_canceled& )
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Timed out waiting for registration result." );
          }

          HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Registration results received." );

          // TODO : what to do with reg result?
        } );
        return true;
      }, task_continuation_context::use_arbitrary() );
    }

    //----------------------------------------------------------------------------
    task<float4x4> RegistrationSystem::WaitForRegistrationResultAsync()
    {
      return create_task( [ = ]() -> float4x4
      {
        while ( true )
        {
          if ( m_registrationResultReceived )
          {
            return m_registrationResult;
          }
          else
          {
            Sleep( 250 );
          }
        }
      } );
    }

    //----------------------------------------------------------------------------
    float4x4 RegistrationSystem::GetRegistrationResult()
    {
      return m_registrationResult;
    }

    //----------------------------------------------------------------------------
    task<void> RegistrationSystem::DataReceiverAsync()
    {
      auto token = m_tokenSource.get_token();
      return create_task( [ = ]()
      {
        bool waitingForHeader = true;
        bool waitingForBody = false;
        DataReader^ reader = ref new DataReader( m_networkPCLSocket->InputStream );
        while ( true )
        {
          if ( token.is_canceled() )
          {
            return;
          }

          if ( waitingForHeader )
          {
            if ( reader->UnconsumedBufferLength >= sizeof( NetworkPCL::PCLMessageHeader ) )
            {
              Platform::Array<byte>^ headerRaw = ref new Platform::Array<byte>( sizeof( NetworkPCL::PCLMessageHeader ) );
              reader->ReadBytes( headerRaw );
              m_nextHeader = *( NetworkPCL::PCLMessageHeader* )headerRaw->Data;

              if ( m_nextHeader.messageType != NetworkPCL::NetworkPCL_KEEP_ALIVE )
              {
                waitingForHeader = false;
                waitingForBody = true;
              }
            }
            else
            {
              Sleep( 100 );
              continue;
            }
          }

          if ( waitingForBody )
          {
            if ( reader->UnconsumedBufferLength >= sizeof( m_nextHeader.bodySize ) )
            {
              Platform::Array<byte>^ body = ref new Platform::Array<byte>( reader->UnconsumedBufferLength );
              reader->ReadBytes( body );

              if ( m_nextHeader.messageType == NetworkPCL::NetworkPCL_REGISTRATION_RESULT )
              {
                if ( body->Length == sizeof( float ) * 16 )
                {
                  // 16 floats
                  XMFLOAT4X4 mat( ( float* )body->Data );
                  XMStoreFloat4x4( &m_registrationResult, XMLoadFloat4x4( &mat ) );
                }
              }

              waitingForHeader = true;
              waitingForBody = false;
            }
            else
            {
              Sleep( 100 );
              continue;
            }
          }
        }
      }, m_tokenSource.get_token() );
    }
  }
}