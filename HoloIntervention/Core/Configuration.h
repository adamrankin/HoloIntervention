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
static const float PRIORITY_REGISTRATION = 1.f; // anchor
static const float PRIORITY_GAZE = 1.f; // gaze cursor
static const float PRIORITY_ICON = 0.5f; // icons
static const float PRIORITY_IMAGING = 3.f; // slices
static const float PRIORITY_CAMERA = 3.f; // spheres (will be changing)
static const float PRIORITY_MANUAL = PRIORITY_NOT_ACTIVE; // has no display component
static const float PRIORITY_OPTICAL = 0.5f; // no display component, for now
static const float PRIORITY_SPLASH = 4.f; // slice
static const float PRIORITY_INVALID_TOOL = 0.25f; // greyscale model
static const float PRIORITY_VALID_TOOL = 2.5f; // colour model
static const float PRIORITY_PHANTOM_TASK = 3.f; // phantom model