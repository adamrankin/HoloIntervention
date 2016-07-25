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
    void Update(float gazeTargetPosition[3], float gazeTargetNormal[3]);
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

    // Gaze origin and direction
    float                                           m_gazeTargetPosition[3];
    float                                           m_gazeTargetNormal[3];

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

    // For devices < DX11.3
    bool                                            m_usingVprtShaders = false;

    // Variables used with the rendering loop.
    bool                                            m_loadingComplete = false;
    bool                                            m_enableCursor = false;
  };
}
