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
#include "Common.h"
#include "SplashSystem.h"

// STL includes
#include <functional>

// WinRT includes
#include <ppltasks.h>

using namespace Concurrency;
using namespace Windows::Storage;
using namespace Windows::Foundation::Numerics;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media::Imaging;

namespace HoloIntervention
{
  namespace System
  {
    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedPosition() const
    {

    }

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedNormal() const
    {

    }

    //----------------------------------------------------------------------------
    float3 SplashSystem::GetStabilizedVelocity() const
    {

    }

    //----------------------------------------------------------------------------
    float SplashSystem::GetStabilizePriority() const
    {

    }

    //----------------------------------------------------------------------------
    SplashSystem::SplashSystem(Rendering::SliceRenderer& sliceRenderer)
      : m_sliceRenderer(sliceRenderer)
    {
      create_task(Windows::ApplicationModel::Package::Current->InstalledLocation->GetFileAsync(L"Assets\\Images\\HoloIntervention.png")).then([this](StorageFile ^ storageFile)
      {
        return storageFile->OpenReadAsync();
      }).then([this](Streams::IRandomAccessStreamWithContentType ^ stream)
      {
        m_splashImage = ref new WriteableBitmap(1, 1);
        m_splashImage->SetSource(stream);

        byte* pixelData = GetDataFromIBuffer<byte>(m_splashImage->PixelBuffer);
      });
    }

    //----------------------------------------------------------------------------
    SplashSystem::~SplashSystem()
    {
    }
  }
}