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
using namespace Windows::Perception::Spatial;
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

      create_task(Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Data\\tool_configuration.xml")).then([this](task<StorageFile^> previousTask)
      {
        StorageFile^ file = nullptr;
        try
        {
          file = previousTask.get();
        }
        catch (Platform::Exception^ e)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to locate tool system configuration file.");
        }

        XmlDocument^ doc = ref new XmlDocument();
        create_task(doc->LoadFromFileAsync(file)).then([this](task<XmlDocument^> previousTask)
        {
          XmlDocument^ doc = nullptr;
          try
          {
            doc = previousTask.get();
          }
          catch (Platform::Exception^ e)
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Tool system configuration file did not contain valid XML.");
          }

          try
          {
            m_transformRepository->ReadConfiguration(doc);
          }
          catch (Platform::Exception^ e)
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Invalid layout in coordinate definitions configuration area.");
          }
        });
      });
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
      return create_task( [ = ]()
      {
        return true;
      } );
    }
  }
}