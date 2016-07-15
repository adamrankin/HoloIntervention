#pragma once

// Local includes
#include <DeviceResources.h>
#include <CameraResources.h>
#include <StepTimer.h>
#include <ShaderStructures.h>

//  WinRT includes
#include <ppltasks.h>

using namespace Windows::Foundation;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  class GazeCursorRenderer
  {
  public:
    GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources );
    void CreateDeviceDependentResources();
    void ReleaseDeviceDependentResources();
    void Update( const DX::StepTimer& timer, SpatialPointerPose^ pointerPose );
    void Render();

    void EnableCursor( bool enable );
    void ToggleCursor();
    bool IsCursorEnabled() const;

    Numerics::float3 GetPosition() const;
    Numerics::float3 GetNormal() const;

  private:
    concurrency::task<void> LoadShadersAsync();

    // Cached pointer to device resources.
    std::shared_ptr<DX::DeviceResources>            m_deviceResources;

    // Direct3D resources for cube geometry.
    Microsoft::WRL::ComPtr<ID3D11InputLayout>       m_inputLayout;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D11VertexShader>      m_vertexShader;
    Microsoft::WRL::ComPtr<ID3D11GeometryShader>    m_geometryShader;
    Microsoft::WRL::ComPtr<ID3D11PixelShader>       m_pixelShader;
    Microsoft::WRL::ComPtr<ID3D11Buffer>            m_modelConstantBuffer;

    // System resources for cube geometry.
    ModelConstantBuffer                             m_modelConstantBufferData;
    uint32                                          m_indexCount = 0;

    // If the current D3D Device supports VPRT, we can avoid using a geometry
    // shader just to set the render target array index.
    // This is for the hololens emulator which may not support dx 11.3
    // which is needed for any stage pipeline render target setting
    bool                                            m_usingVprtShaders = false;

    // Variables used with the rendering loop.
    bool                                            m_loadingComplete = false;
    bool                                            m_enableCursor = false;
  };
}
