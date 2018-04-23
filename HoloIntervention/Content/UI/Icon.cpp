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
#include "Icon.h"

// Rendering includes
#include "Content/Rendering/Model/Model.h"

using namespace Windows::Foundation::Numerics;

namespace HoloIntervention
{
  namespace UI
  {
    //----------------------------------------------------------------------------
    Icon::Icon()
    {
    }

    //----------------------------------------------------------------------------
    Icon::~Icon()
    {

    }

    //----------------------------------------------------------------------------
    void Icon::SetId(uint64 id)
    {
      m_id = id;
    }

    //----------------------------------------------------------------------------
    uint64 Icon::GetId() const
    {
      return m_id;
    }

    //----------------------------------------------------------------------------
    std::shared_ptr<HoloIntervention::Rendering::Model> Icon::GetModel() const
    {
      return m_model;
    }

    //----------------------------------------------------------------------------
    void Icon::SetModel(std::shared_ptr<Rendering::Model> entry)
    {
      m_model = entry;
      m_rotatedBounds = m_model->GetBounds(m_userRotation);
    }

    //----------------------------------------------------------------------------
    void Icon::SetUserRotation(float pitch, float yaw, float roll)
    {
      SetUserRotation(make_quaternion_from_yaw_pitch_roll(yaw, pitch, roll));
    }

    //----------------------------------------------------------------------------
    void Icon::SetUserRotation(quaternion rotation)
    {
      m_userRotation = make_float4x4_from_quaternion(rotation);
      m_rotatedBounds = m_model->GetBounds(m_userRotation);

      // Recalculate scaling based on rotated model
    }

    //----------------------------------------------------------------------------
    void Icon::SetUserRotation(float4x4 rotation)
    {
      float3 scale;
      quaternion rotationQuat;
      float3 transformation;
      if (!decompose(rotation, &scale, &rotationQuat, &transformation))
      {
        return;
      }

      SetUserRotation(rotationQuat);
    }

    //----------------------------------------------------------------------------
    Windows::Foundation::Numerics::float4x4 Icon::GetUserRotation() const
    {
      return m_userRotation;
    }

    //----------------------------------------------------------------------------
    std::array<float, 6> Icon::GetRotatedBounds() const
    {
      return m_rotatedBounds;
    }

    //----------------------------------------------------------------------------
    bool Icon::GetFirstFrame() const
    {
      return m_firstFrame;
    }

    //----------------------------------------------------------------------------
    void Icon::SetFirstFrame(bool firstFrame)
    {
      m_firstFrame = firstFrame;
    }

    //----------------------------------------------------------------------------
    uint64 Icon::GetUserValueNumber() const
    {
      return m_userValueNumber;
    }

    //----------------------------------------------------------------------------
    void Icon::SetUserValue(uint64 userValue)
    {
      m_userValueNumber = userValue;
    }

    //----------------------------------------------------------------------------
    void Icon::SetUserValue(const std::wstring& userValue)
    {
      m_userValueString = userValue;
    }

    //----------------------------------------------------------------------------
    std::wstring Icon::GetUserValueString() const
    {
      return m_userValueString;
    }
  }
}
