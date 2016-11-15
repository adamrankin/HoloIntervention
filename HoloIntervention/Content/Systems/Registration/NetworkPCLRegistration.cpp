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
#include "NetworkPCLRegistration.h"

// System includes
#include "SpatialSystem.h"
#include "NotificationSystem.h"

// Network includes
#include "IGTLinkIF.h"

// Spatial includes
#include "SurfaceMesh.h"

// OpenIGTLink includes
#include <igtlutil/igtl_util.h>

// std includes
#include <sstream>

// DirectXTex includes
#include <DirectXTex.h>

using namespace DirectX;
using namespace Windows::Data::Xml::Dom;
using namespace Windows::Networking;
using namespace Windows::Perception::Spatial::Surfaces;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    NetworkPCLRegistration::NetworkPCLRegistration()
    {
      create_task(Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Data\\configuration.xml")).then([this](task<StorageFile^> previousTask)
      {
        StorageFile^ file = nullptr;
        try
        {
          file = previousTask.get();
        }
        catch (Platform::Exception^ e)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to locate system configuration file.");
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
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"System configuration file did not contain valid XML.");
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
    NetworkPCLRegistration::~NetworkPCLRegistration()
    {
      m_tokenSource.cancel();
      if (m_receiverTask != nullptr)
      {
        m_receiverTask->wait();
      }
      m_connected = false;
    }

    //----------------------------------------------------------------------------
    void NetworkPCLRegistration::Update(SpatialCoordinateSystem^ coordinateSystem)
    {
      // Point collection logic
      if (m_collectingPoints && HoloIntervention::instance()->GetIGTLink().IsConnected())
      {
        if (HoloIntervention::instance()->GetIGTLink().GetTrackedFrame(m_trackedFrame, &m_latestTimestamp))
        {
          m_transformRepository->SetTransforms(m_trackedFrame);
          bool isValid;
          float4x4 stylusTipToReference;
          try
          {
            stylusTipToReference = m_transformRepository->GetTransform(m_stylusTipToReferenceName, &isValid);
            // Put into column order so that win numerics functions work as expected
            stylusTipToReference = stylusTipToReference * make_float4x4_scale(1.f / 1000.f);   // Scale from mm to m
            stylusTipToReference = transpose(stylusTipToReference);
            if (isValid)
            {
              float3 point = translation(stylusTipToReference);
              m_points.push_back(point);
            }
          }
          catch (Platform::Exception^ e)
          {
            OutputDebugStringW(e->Message->Data());
          }
        }
      }
    }

    //----------------------------------------------------------------------------
    void NetworkPCLRegistration::StartCollectingPoints()
    {
      m_points.clear();
      m_latestTimestamp = 0;
      m_collectingPoints = true;
    }

    //----------------------------------------------------------------------------
    void NetworkPCLRegistration::EndCollectingPoints()
    {
      m_collectingPoints = false;
      if (m_points.size() == 0)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"No points collected.");
        return;
      }
      HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Collecting finished.");

      SendRegistrationDataAsync().then([this](task<bool> previousTask)
      {
        bool result(false);
        try
        {
          result = previousTask.get();
        }
        catch (const std::exception& e)
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to send registration data.");
          OutputDebugStringA(e.what());
        }
        if (result)
        {
          std::stringstream ss;
          ss << m_points.size() << " points collected. Computing registration...";
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(ss.str());
        }
      });
    }

    //----------------------------------------------------------------------------
    void NetworkPCLRegistration::SetSpatialMesh(std::shared_ptr<Spatial::SurfaceMesh> mesh)
    {
      m_spatialMesh = mesh;
    }


    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 NetworkPCLRegistration::GetRegistrationResult()
    {
      return m_registrationResult;
    }

    //----------------------------------------------------------------------------
    void NetworkPCLRegistration::RegisterVoiceCallbacks(HoloIntervention::Sound::VoiceInputCallbackMap& callbacks)
    {
      callbacks[L"start collecting points"] = [this](SpeechRecognitionResult ^ result)
      {
        if (HoloIntervention::instance()->GetIGTLink().IsConnected())
        {
          StartCollectingPoints();
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Collecting points...");
        }
        else
        {
          HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Not connected!");
        }
      };

      callbacks[L"end collecting points"] = [this](SpeechRecognitionResult ^ result)
      {
        HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Point collection not active.");
        return;

        EndCollectingPoints();
      };
    }

    //----------------------------------------------------------------------------
    task<bool> NetworkPCLRegistration::SendRegistrationDataAsync()
    {
      auto sendTask = create_task([ = ]() -> bool
      {
        auto hostname = ref new HostName(ref new Platform::String(HoloIntervention::instance()->GetIGTLink().GetHostname().c_str()));

        if (!m_connected)
        {
          auto connectTask = create_task(m_networkPCLSocket->ConnectAsync(hostname, ref new Platform::String(L"24012")));

          try
          {
            connectTask.wait();
          }
          catch (Platform::Exception^ e)
          {
            HoloIntervention::instance()->GetNotificationSystem().QueueMessage(L"Unable to connect to NetworkPCL.");
            return false;
          }

          m_connected = true;
        }

        DataWriter^ writer = ref new DataWriter(m_networkPCLSocket->OutputStream);
        SpatialSurfaceMesh^ mesh = m_spatialMesh->GetSurfaceMesh();
        float4x4 meshToWorld = m_spatialMesh->GetMeshToWorldTransform();

        auto bodySize = mesh->TriangleIndices->ElementCount * sizeof(float) * 3 + m_points.size() * sizeof(float) * 3;

        // First, write header details to the stream
        NetworkPCL::PCLMessageHeader header;
        header.messageType = NetworkPCL::NetworkPCL_POINT_DATA;
        header.additionalHeaderSize = 0;
        header.bodySize = bodySize;
        header.referenceVertexCount = mesh->TriangleIndices->ElementCount;
        header.targetVertexCount = m_points.size();
        header.SwapLittleEndian();
        writer->WriteBytes(Platform::ArrayReference<byte>((byte*)&header, sizeof(NetworkPCL::PCLMessageHeader)));

        // Convert the mesh data to a stream of vertices, de-indexed
        float* verticesComponents = GetDataFromIBuffer<float>(mesh->VertexPositions->Data);
        std::vector<XMFLOAT3> vertices;
        for (unsigned int i = 0; i < mesh->VertexPositions->ElementCount; ++i)
        {
          XMFLOAT3 vertex(verticesComponents[0], verticesComponents[1], verticesComponents[2]);

          // TODO : does this return the right result? row/column matrix order
          // Transform it into world coordinates
          XMStoreFloat3(&vertex, XMVector3Transform(XMLoadFloat3(&vertex), XMMatrixTranspose(XMLoadFloat4x4(&meshToWorld))));

          // Store it
          vertices.push_back(vertex);

          verticesComponents += 3;
          if (HasAlpha((DXGI_FORMAT)mesh->VertexPositions->Format))
          {
            // Skip alpha value
            verticesComponents++;
          }
        }

        uint32* indicies = GetDataFromIBuffer<uint32>(mesh->TriangleIndices->Data);
        std::vector<uint32> indiciesVector;
        for (unsigned int i = 0; i < mesh->TriangleIndices->ElementCount; ++i)
        {
          indiciesVector.push_back(*indicies);
          indicies++;
        }

        for (unsigned int i = 0; i < indiciesVector.size(); i++)
        {
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&vertices[indiciesVector[i]].x)[j]);
          }
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&vertices[indiciesVector[i]].y)[j]);
          }
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&vertices[indiciesVector[i]].z)[j]);
          }
        }

        for (auto& point : m_points)
        {
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&point.x)[j]);
          }
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&point.y)[j]);
          }
          for (int j = 0; j < 4; ++j)
          {
            writer->WriteByte(((byte*)&point.z)[j]);
          }
        }

        auto storeTask = create_task(writer->StoreAsync()).then([ = ](task<uint32> writeTask)
        {
          uint32 bytesWritten;
          try
          {
            bytesWritten = writeTask.get();
          }
          catch (Platform::Exception^ exception)
          {
            std::wstring message(exception->Message->Data());
            std::string messageStr(message.begin(), message.end());
            throw std::exception(messageStr.c_str());
          }

          if (bytesWritten != bodySize + sizeof(NetworkPCL::PCLMessageHeader))
          {
            throw std::exception("Entire message couldn't be sent.");
          }

          // Start the asynchronous data receiver thread
          m_registrationResultReceived = false;

          return bytesWritten;
        });

        int bytesWritten;
        try
        {
          bytesWritten = storeTask.get();
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }
        catch (Platform::Exception^ e)
        {
          OutputDebugStringW(e->Message->Data());
        }

        if (bytesWritten > 0)
        {
          return true;
        }
        return false;
      }, task_continuation_context::use_arbitrary());

      sendTask.then([ = ](task<bool> previousTask)
      {
        bool result(false);
        try
        {
          result = previousTask.get();
        }
        catch (const std::exception& e)
        {
          OutputDebugStringA(e.what());
        }
        catch (Platform::Exception^ e)
        {
          OutputDebugStringW(e->Message->Data());
        }

        if (result)
        {
          m_receiverTask = &DataReceiverAsync();
          try
          {
            m_receiverTask->wait();
          }
          catch (const std::exception& e)
          {
            OutputDebugStringA(e.what());
          }
          catch (Platform::Exception^ e)
          {
            OutputDebugStringW(e->Message->Data());
          }
        }
      });

      return sendTask;
    }

    //----------------------------------------------------------------------------
    task<void> NetworkPCLRegistration::DataReceiverAsync()
    {
      auto token = m_tokenSource.get_token();
      return create_task([ = ]()
      {
        bool waitingForHeader = true;
        bool waitingForBody = false;
        DataReader^ reader = ref new DataReader(m_networkPCLSocket->InputStream);
        while (true)
        {
          if (token.is_canceled())
          {
            return;
          }

          if (waitingForHeader)
          {
            auto readTask = create_task(reader->LoadAsync(sizeof(NetworkPCL::PCLMessageHeader)));
            int bytesRead(-1);
            try
            {
              bytesRead = readTask.get();
            }
            catch (const std::exception& e)
            {
              OutputDebugStringA(e.what());
            }
            catch (Platform::Exception^ e)
            {
              OutputDebugStringW(e->Message->Data());
            }
            if (bytesRead != sizeof(NetworkPCL::PCLMessageHeader))
            {
              throw std::exception("Bad read over network.");
            }

            auto buffer = reader->ReadBuffer(sizeof(NetworkPCL::PCLMessageHeader));
            auto header = GetDataFromIBuffer<byte>(buffer);
            m_nextHeader = *(NetworkPCL::PCLMessageHeader*)header;
            m_nextHeader.SwapLittleEndian();

            // Drop any additional header data
            if (m_nextHeader.additionalHeaderSize > 0)
            {
              readTask = create_task(reader->LoadAsync(m_nextHeader.additionalHeaderSize));
              try
              {
                readTask.wait();
                reader->ReadBuffer(m_nextHeader.additionalHeaderSize);
              }
              catch (const std::exception& e)
              {
                OutputDebugStringA(e.what());
              }
            }

            if (m_nextHeader.messageType != NetworkPCL::NetworkPCL_KEEP_ALIVE)
            {
              waitingForHeader = false;
              waitingForBody = true;
            }
          }

          if (waitingForBody)
          {
            auto readTask = create_task(reader->LoadAsync(m_nextHeader.bodySize));
            int bytesRead(-1);
            try
            {
              bytesRead = readTask.get();
            }
            catch (const std::exception& e)
            {
              OutputDebugStringA(e.what());
            }
            if (bytesRead != m_nextHeader.bodySize)
            {
              throw std::exception("Bad read over network.");
            }

            auto buffer = reader->ReadBuffer(m_nextHeader.bodySize);
            auto body = GetDataFromIBuffer<float>(buffer);

            if (m_nextHeader.messageType == NetworkPCL::NetworkPCL_REGISTRATION_RESULT)
            {
              // 16 floats
              XMFLOAT4X4 mat(body);
              XMStoreFloat4x4(&m_registrationResult, XMLoadFloat4x4(&mat));
            }

            waitingForHeader = true;
            waitingForBody = false;
          }
        }
      }, m_tokenSource.get_token());
    }
  }
}

//----------------------------------------------------------------------------
void NetworkPCL::PCLMessageHeader::SwapLittleEndian()
{
  if (igtl_is_little_endian())
  {
    messageType = BYTE_SWAP_INT16(messageType);
    additionalHeaderSize = BYTE_SWAP_INT32(additionalHeaderSize);
    bodySize = BYTE_SWAP_INT32(bodySize);
    referenceVertexCount = BYTE_SWAP_INT32(referenceVertexCount);
    targetVertexCount = BYTE_SWAP_INT32(targetVertexCount);
  }
}