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

#pragma once

// STL includes
#include <map>
#include <string>

// Engine component priority values, winner gets to set stabilization parameters
static const float PRIORITY_NOT_ACTIVE = 0.f;
static const float REGISTRATION_PRIORITY = 1.f; // anchor
static const float GAZE_PRIORITY = 1.f; // gaze cursor
static const float ICON_PRIORITY = 0.5f; // icons
static const float IMAGING_PRIORITY = 3.f; // slices
static const float CAMERA_PRIORITY = 3.f; // spheres (will be changing)
static const float MANUAL_PRIORITY = PRIORITY_NOT_ACTIVE; // has no display component
static const float OPTICAL_PRIORITY = 0.5f; // no display component, for now
static const float SPLASH_PRIORITY = 4.f; // slice
static const float INVALID_TOOL_PRIORITY = 0.25f; // greyscale model
static const float VALID_TOOL_PRIORITY = 2.5f; // colour model