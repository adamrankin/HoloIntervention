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

using namespace Concurrency;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Networking;
using namespace Windows::Perception::Spatial;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

namespace
{
  struct PCLMessageHeader
  {
    uint16_t  additionalHeaderSize;
    uint32_t  bodySize;
    uint32_t  referenceVertexCount;
    uint32_t  targetVertexCount;
  };

  // Creates a task that completes after the specified delay.
  task<void> complete_after( unsigned int timeoutMs )
  {
    // A task completion event that is set when a timer fires.
    task_completion_event<void> tce;

    // Create a non-repeating timer.
    auto fire_once = new timer<int>( timeoutMs, 0, nullptr, false );
    // Create a call object that sets the completion event after the timer fires.
    auto callback = new call<int>( [tce]( int )
    {
      tce.set();
    } );

    // Connect the timer to the callback and start the timer.
    fire_once->link_target( callback );
    fire_once->start();

    // Create a task that completes after the completion event is set.
    task<void> event_set( tce );

    // Create a continuation task that cleans up resources and
    // and return that continuation task.
    return event_set.then( [callback, fire_once]()
    {
      delete callback;
      delete fire_once;
    } );
  }

  // Cancels the provided task after the specified delay, if the task
  // did not complete.
  template<typename T>
  task<T> cancel_after_timeout( task<T> t, cancellation_token_source cts, unsigned int timeout )
  {
    // Create a task that returns true after the specified task completes.
    task<bool> success_task = t.then( []( T )
    {
      return true;
    } );
    // Create a task that returns false after the specified timeout.
    task<bool> failure_task = complete_after( timeout ).then( []
    {
      return false;
    } );

    // Create a continuation task that cancels the overall task
    // if the timeout task finishes first.
    return ( failure_task || success_task ).then( [t, cts]( bool success )
    {
      if ( !success )
      {
        // Set the cancellation token. The task that is passed as the
        // t parameter should respond to the cancellation and stop
        // as soon as it can.
        cts.cancel();
      }

      // Return the original task.
      return t;
    } );
  }
}

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
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Collecting finished." );

        SendRegistrationDataAsync().then( [this]( bool result )
        {
          if ( result )
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage( L"Computing registration..." );
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

        cancellation_token_source cts;
        auto connectTask = cancel_after_timeout( create_task( m_networkPCLSocket->ConnectAsync( hostname, ref new Platform::String( L"24012" ) ) ), cts, 1500 );

        try
        {
          connectTask.wait();
        }
        catch ( const task_canceled& )
        {
          return false;
        }

        DataWriter^ writer = ref new DataWriter( m_networkPCLSocket->OutputStream );
        SpatialSurfaceMesh^ mesh = m_spatialMesh->GetSurfaceMesh();

        // First, write header details to the stream
        writer->WriteUInt16( 0 ); // no additional header data

        // Calculate the body size
        unsigned int bodySize = mesh->TriangleIndices->ElementCount * sizeof( float ) * 3 + m_points.size() * sizeof( float ) * 3;
        writer->WriteUInt32( bodySize );
        writer->WriteUInt32( mesh->TriangleIndices->ElementCount );
        writer->WriteUInt32( m_points.size() );

        // Convert the mesh data to a stream of vertices, de-indexed
        auto reader = DataReader::FromBuffer( mesh->VertexPositions->Data );
        std::vector<std::array<float, 3>> vertexPosition;
        for ( unsigned int i = 0; i < mesh->VertexPositions->ElementCount; ++i )
        {
          vertexPosition.push_back( std::array<float, 3> { reader->ReadSingle(), reader->ReadSingle(), reader->ReadSingle() } );
        }

        std::vector<uint16> triangleIndices;
        for ( unsigned int i = 0; i < mesh->TriangleIndices->ElementCount; ++i )
        {
          triangleIndices.push_back( reader->ReadUInt16() );
        }

        for ( unsigned int i = 0; i < triangleIndices.size(); i++ )
        {
          writer->WriteSingle( vertexPosition[triangleIndices[i]][0] );
          writer->WriteSingle( vertexPosition[triangleIndices[i]][1] );
          writer->WriteSingle( vertexPosition[triangleIndices[i]][2] );
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

          if ( bytesWritten != bodySize + sizeof( PCLMessageHeader ) )
          {
            throw std::exception( "Entire message couldn't be sent." );
          }

          create_task( [ = ]()
          {
            DataReader^ reader = ref new DataReader( m_networkPCLSocket->InputStream );
            while ( true )
            {
              if ( reader->UnconsumedBufferLength > 0 )
              {

              }
            }
          } );
        } );
        return true;
      }, concurrency::task_continuation_context::use_arbitrary() );
    }
  }
}