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

#pragma once

// STL includes
#include <vector>

// WinRt includes
#include <ppltasks.h>

// OpenCV includes
#include <opencv2/core.hpp>

namespace HoloIntervention
{
  namespace System
  {
    class LandmarkRegistration
    {
    public:
      typedef std::vector<Windows::Foundation::Numerics::float2> VecFloat2;
      typedef std::vector<Windows::Foundation::Numerics::float3> VecFloat3;
      typedef std::vector<Windows::Foundation::Numerics::float4> VecFloat4;
      typedef std::vector<Windows::Foundation::Numerics::float4x4> VecFloat4x4;
      typedef std::vector<VecFloat3> DetectionFrames;
      typedef std::vector<cv::Point3f> LandmarkListCv;

      LandmarkRegistration();
      ~LandmarkRegistration();

      void SetSourceLandmarks(const DetectionFrames& frames);
      void SetTargetLandmarks(const DetectionFrames& frames);
      void SetSourceLandmarks(const VecFloat3& landmarks);
      void SetTargetLandmarks(const VecFloat3& landmarks);
      void SetSourceLandmarks(const LandmarkListCv& landmarks);
      void SetTargetLandmarks(const LandmarkListCv& landmarks);

      Concurrency::task<Windows::Foundation::Numerics::float4x4> CalculateTransformationAsync();

    protected:
      VecFloat3 m_sourceLandmarks;
      VecFloat3 m_targetLandmarks;
    };
  }
}