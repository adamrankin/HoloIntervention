//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#pragma once

// Windows includes
#include <concrt.h>
#include <mmreg.h>
#include <wtypes.h>

// STL includes
#include <vector>

namespace Valhalla
{
  namespace Sound
  {
    // Helper class uses MediaFoundation to read audio files
    class AudioFileReader
    {
    public:
      virtual ~AudioFileReader() {}
      Concurrency::task<HRESULT> InitializeAsync(_In_ const std::wstring& filename);

      const WAVEFORMATEX* GetFormat() const
      {
        return &_format;
      }

      size_t GetSize() const
      {
        return _audioData.size();
      }

      const BYTE* GetData() const
      {
        return _audioData.data();
      }

    private:
      WAVEFORMATEX        _format;
      std::vector<BYTE>   _audioData;
    };
  }
}