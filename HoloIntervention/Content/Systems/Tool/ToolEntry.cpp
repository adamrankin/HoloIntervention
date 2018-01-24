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
#include "ToolEntry.h"

// UI includes
#include "Icons.h"

// System includes
#include "NetworkSystem.h"

// Rendering includes
#include "ModelEntry.h"
#include "ModelRenderer.h"

// Algorithm includes
#include "KalmanFilter.h"

// STL includes
#include <string>
#include <sstream>

// OpenCV includes
#include <opencv2/core/mat.hpp>

using namespace Concurrency;
using namespace Windows::Foundation::Collections;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Input::Spatial;
using namespace cv;

namespace HoloIntervention
{
  namespace Tools
  {
    //----------------------------------------------------------------------------
    float3 ToolEntry::GetStabilizedPosition(SpatialPointerPose^ pose) const
    {
      if (m_modelEntry != nullptr && m_modelEntry->IsLoaded())
      {
        return transform(float3(0.f, 0.f, 0.f), m_modelEntry->GetCurrentPose());
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float3 ToolEntry::GetStabilizedVelocity() const
    {
      if (m_modelEntry != nullptr && m_modelEntry->IsLoaded())
      {
        return m_modelEntry->GetVelocity();
      }
      return float3(0.f, 0.f, 0.f);
    }

    //----------------------------------------------------------------------------
    float ToolEntry::GetStabilizePriority() const
    {
      if (!m_modelEntry->IsLoaded() || !m_modelEntry->IsInFrustum())
      {
        return PRIORITY_NOT_ACTIVE;
      }

      return m_wasValid ? PRIORITY_VALID_TOOL : PRIORITY_INVALID_TOOL;
    }

    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry(Rendering::ModelRenderer& modelRenderer,
                         System::NetworkSystem& networkSystem,
                         UI::Icons& icons,
                         uint64 hashedConnectionName,
                         UWPOpenIGTLink::TransformName^ coordinateFrame,
                         UWPOpenIGTLink::TransformRepository^ transformRepository,
                         Platform::String^ userId)
      : m_modelRenderer(modelRenderer)
      , m_networkSystem(networkSystem)
      , m_icons(icons)
      , m_hashedConnectionName(hashedConnectionName)
      , m_transformRepository(transformRepository)
      , m_coordinateFrame(coordinateFrame)
      , m_userId(std::wstring(userId->Data()))
      , m_kalmanFilter(std::make_shared<Algorithm::KalmanFilter>(18, 6, 0))
    {
      m_componentReady = true;
    }

    //----------------------------------------------------------------------------
    ToolEntry::ToolEntry(Rendering::ModelRenderer& modelRenderer,
                         System::NetworkSystem& networkSystem,
                         UI::Icons& icons,
                         uint64 hashedConnectionName,
                         const std::wstring& coordinateFrame,
                         UWPOpenIGTLink::TransformRepository^ transformRepository,
                         Platform::String^ userId)
      : m_modelRenderer(modelRenderer)
      , m_networkSystem(networkSystem)
      , m_icons(icons)
      , m_hashedConnectionName(hashedConnectionName)
      , m_transformRepository(transformRepository)
      , m_userId(std::wstring(userId->Data()))
      , m_kalmanFilter(std::make_shared<Algorithm::KalmanFilter>(21, 7, 0)) // position as x, y, z -- rotation as quaternion x, y, z, w
    {
      m_coordinateFrame = ref new UWPOpenIGTLink::TransformName(ref new Platform::String(coordinateFrame.c_str()));

      // Populate transition matrix based on position/velocity/acceleration functions for x, y, z, q_x, q_y, q_z, q_w
      // x+1 = x + v_x + 0.5a_x
      // y+1 = y + v_y + 0.5a_y
      // z+1 = z + v_z + 0.5a_z
      // q_x+1 = q_x + v_q_x + 0.5a_q_x
      // q_y+1 = q_y + v_q_y + 0.5a_q_y
      // q_z+1 = q_z + v_q_z + 0.5a_q_z
      // q_w+1 = q_w + v_q_w + 0.5a_q_w
      // v_x+1 = v_x + a_x
      // v_y+1 = v_y + a_y
      // v_z+1 = v_z + a_z
      // v_q_x+1 = v_q_x + a_q_x
      // v_q_y+1 = v_q_y + a_q_y
      // v_q_z+1 = v_q_z + a_q_z
      // v_q_w+1 = v_q_w + a_q_w
      // a_x+1 = a_x
      // a_y+1 = a_y
      // a_z+1 = a_z
      // a_q_x+1 = a_q_x
      // a_q_y+1 = a_q_y
      // a_q_z+1 = a_q_z
      // a_q_w+1 = a_q_w
      float transitionMatrixCoefficients[441] = { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.5f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f,
                                                  0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f
                                                };
      m_kalmanFilter->SetTransitionMatrix(Mat_<float>(21, 21, transitionMatrixCoefficients));
    }

    //----------------------------------------------------------------------------
    ToolEntry::~ToolEntry()
    {
    }

    //----------------------------------------------------------------------------
    void ToolEntry::Update(const DX::StepTimer& timer)
    {
      bool registrationTransformValid = m_transformRepository->GetTransformValid(ref new UWPOpenIGTLink::TransformName(L"Reference", L"HMD"));
      if (!registrationTransformValid)
      {
        m_modelEntry->SetVisible(false);
        m_iconEntry->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        return;
      }
      else
      {
        m_modelEntry->SetVisible(true);
      }

      // m_transformRepository has already been initialized with the network transforms for this update
      auto toolToRefTransform = m_networkSystem.GetTransform(m_hashedConnectionName, m_coordinateFrame, m_latestTimestamp);

      if (toolToRefTransform == nullptr)
      {
        // No new transform since last timestamp
        return;
      }

      m_latestTimestamp = toolToRefTransform->Timestamp;
      m_transformRepository->SetTransform(m_coordinateFrame, toolToRefTransform->Matrix, toolToRefTransform->Valid);

      IKeyValuePair<bool, float4x4>^ result = m_transformRepository->GetTransform(ref new UWPOpenIGTLink::TransformName(m_coordinateFrame->From(), L"HMD"));
      m_isValid = result->Key;
      if (!m_isValid && m_wasValid)
      {
        m_wasValid = false;
        if (m_modelEntry != nullptr)
        {
          m_modelEntry->RenderGreyscale();
        }
        if (m_iconEntry != nullptr)
        {
          m_iconEntry->GetModelEntry()->SetRenderingState(Rendering::RENDERING_GREYSCALE);
        }
        return;
      }

      if (m_isValid)
      {
        if (!m_wasValid)
        {
          if (m_iconEntry != nullptr)
          {
            m_iconEntry->GetModelEntry()->SetRenderingState(Rendering::RENDERING_DEFAULT);
          }
          if (m_modelEntry != nullptr)
          {
            m_modelEntry->RenderDefault();
          }
        }

        /*
        quaternion q_measured = make_quaternion_from_rotation_matrix(transform);
        if (m_firstDataPoint)
        {
          std::vector<float> statePre;
          statePre.push_back(transform.m41);
          statePre.push_back(transform.m42);
          statePre.push_back(transform.m43);
          statePre.push_back(q_measured.x);
          statePre.push_back(q_measured.y);
          statePre.push_back(q_measured.z);
          statePre.push_back(q_measured.w);
          m_kalmanFilter->SetStatePre(statePre);
          m_firstDataPoint = false;
        }

        auto mat = m_kalmanFilter->Predict();
        quaternion q;
        q.x = mat.at<float>(3);
        q.y = mat.at<float>(4);
        q.z = mat.at<float>(5);
        q.w = mat.at<float>(6);
        auto predictedTransform = make_float4x4_from_quaternion(q);
        predictedTransform.m41 = mat.at<float>(0);
        predictedTransform.m42 = mat.at<float>(1);
        predictedTransform.m43 = mat.at<float>(2);
        m_modelEntry->SetDesiredPose(predictedTransform);

        m_correctionMatrix.at<float>(0) = transform.m41;
        m_correctionMatrix.at<float>(1) = transform.m42;
        m_correctionMatrix.at<float>(2) = transform.m43;
        m_correctionMatrix.at<float>(3) = q_measured.x;
        m_correctionMatrix.at<float>(4) = q_measured.y;
        m_correctionMatrix.at<float>(5) = q_measured.w;
        m_correctionMatrix.at<float>(6) = q_measured.z;

        m_kalmanFilter->Correct(m_correctionMatrix);
        */
        if (m_modelEntry != nullptr)
        {
          m_modelEntry->SetDesiredPose(transpose(result->Value));
        }
        m_wasValid = true;
      }
    }

    //----------------------------------------------------------------------------
    task<void> ToolEntry::SetModelEntryAsync(std::shared_ptr<Rendering::ModelEntry> entry)
    {
      return create_task([this, entry]()
      {

        if (m_modelEntry == entry)
        {
          return create_task([]() {});
        }

        if (m_iconEntry != nullptr)
        {
          m_icons.RemoveEntry(m_iconEntry->GetId());
        }

        return m_icons.AddEntryAsync(entry, 0).then([this, entry](std::shared_ptr<UI::IconEntry> iconEntry) -> void
        {
          m_modelEntry = entry;
          m_iconEntry = iconEntry;
          iconEntry->GetModelEntry()->SetVisible(true);
        });
      });
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<Rendering::ModelEntry> ToolEntry::GetModelEntry()
    {
      return m_modelEntry;
    }

    //----------------------------------------------------------------------------
    UWPOpenIGTLink::TransformName^ ToolEntry::GetCoordinateFrame() const
    {
      return m_coordinateFrame;
    }

    //----------------------------------------------------------------------------
    bool ToolEntry::IsValid() const
    {
      return m_isValid;
    }

    //----------------------------------------------------------------------------
    bool ToolEntry::WasValid() const
    {
      return m_wasValid;
    }

    //----------------------------------------------------------------------------
    uint64 ToolEntry::GetId() const
    {
      if (m_modelEntry == nullptr)
      {
        return INVALID_TOKEN;
      }
      return m_modelEntry->GetId();
    }

    //----------------------------------------------------------------------------
    std::wstring ToolEntry::GetUserId() const
    {
      return m_userId;
    }

  }
}