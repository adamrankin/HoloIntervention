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

#include "Math.h"

namespace HoloIntervention
{
  namespace Algorithm
  {
    class PointToLineRegistration
    {
    public:
      void AddPoint(const Point& point);
      void AddLine(const Line& line);

      void Reset();

      Concurrency::task<Windows::Foundation::Numerics::float4x4> ComputeAsync(float& outError);

    public:
      PointToLineRegistration();
      ~PointToLineRegistration();

    protected:
      std::vector<Point>      m_points;
      std::vector<Line>       m_lines;

      const float             EXIT_CONDITION_TOLERANCE = 1e-9f;
    };
  }
}