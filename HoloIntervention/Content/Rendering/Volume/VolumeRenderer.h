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

// Local includes
#include "PiecewiseLinearTF.h"

namespace DX
{
  class DeviceResources;
  class StepTimer;
}

namespace HoloIntervention
{
  namespace Rendering
  {
    struct VolumeConstantBuffer
    {
      DirectX::XMFLOAT4X4 worldMatrix;
    };
    static_assert((sizeof(VolumeConstantBuffer) % (sizeof(float) * 4)) == 0, "Model constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");

    struct VertexPosition
    {
      Windows::Foundation::Numerics::float3 pos;
    };

    class VolumeRenderer
    {
    public:
      enum TransferFunctionType
      {
        TransferFunction_Piecewise_Linear,
      };
      static const uint32_t INVALID_VOLUME_INDEX = 0;

    public:
      VolumeRenderer(const std::shared_ptr<DX::DeviceResources>& deviceResources);
      ~VolumeRenderer();

      void Update(Windows::UI::Input::Spatial::SpatialPointerPose^ pose, const DX::StepTimer& timer);
      void Render();

      Concurrency::task<void> SetTransferFunctionTypeAsync(TransferFunctionType type);

      // D3D device related controls
      Concurrency::task<void> CreateDeviceDependentResourcesAsync();
      void ReleaseDeviceDependentResources();

    protected:
      void CreateVertexResources();
      void ReleaseVertexResources();

    protected:
      // Cached pointer to device resources.
      std::shared_ptr<DX::DeviceResources>              m_deviceResources;

      // Direct3D resources for quad geometry.
      Microsoft::WRL::ComPtr<ID3D11InputLayout>         m_inputLayout;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_vertexBuffer;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_indexBuffer;
      Microsoft::WRL::ComPtr<ID3D11VertexShader>        m_vertexShader;
      Microsoft::WRL::ComPtr<ID3D11GeometryShader>      m_geometryShader;
      Microsoft::WRL::ComPtr<ID3D11PixelShader>         m_pixelShader;
      Microsoft::WRL::ComPtr<ID3D11Buffer>              m_modelConstantBuffer;

      uint32                                            m_indexCount = 0;
      std::atomic_bool                                  m_loadingComplete = false;
      bool                                              m_usingVprtShaders = false;
      uint32                                            m_nextUnusedId = 1; // start at 1
      TransferFunctionType                              m_tfType = TransferFunction_Piecewise_Linear;
      std::mutex                                        m_tfMutex;
      ITransferFunction*                                m_transferFunction = new PiecewiseLinearTF();
    };
  }
}