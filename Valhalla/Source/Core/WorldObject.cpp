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
#include "WorldObject.h"

namespace Valhalla
{
  //----------------------------------------------------------------------------
  WorldObject::WorldObject()
  {
  }

  //----------------------------------------------------------------------------
  WorldObject::~WorldObject()
  {
  }

  //----------------------------------------------------------------------------
  void WorldObject::Update()
  {

  }

  //----------------------------------------------------------------------------
  void WorldObject::SetCoordinateSystem(Windows::Perception::Spatial::SpatialCoordinateSystem^ system)
  {
    m_coordinateSystem = system;
  }

  //----------------------------------------------------------------------------
  Windows::Perception::Spatial::SpatialCoordinateSystem^ WorldObject::GetCoordinateSystem() const
  {
    return m_coordinateSystem;
  }

  //----------------------------------------------------------------------------
  void WorldObject::SetOOBB(const std::array<float, 6>& oobb)
  {
    m_objectOrientedBoundingBox = oobb;
  }

  //----------------------------------------------------------------------------
  std::array<float, 6> WorldObject::GetOOBB() const
  {
    return m_objectOrientedBoundingBox;
  }
}