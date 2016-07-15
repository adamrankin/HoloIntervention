#pragma once

// Local includes
#include <DeviceResources.h>
#include <StepTimer.h>

// DirectXTK includes
#include <Effects.h>
#include <CommonStates.h>
#include <Model.h>
#include <SimpleMath.h>

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

  private:
    // Cached pointer to device resources.
    std::shared_ptr<DX::DeviceResources> m_deviceResources;

    // Resources for model rendering
    DirectX::SimpleMath::Matrix m_viewMatrix;
    DirectX::SimpleMath::Matrix m_projectionMatrix;
    DirectX::SimpleMath::Matrix m_modelMatrix;
    std::unique_ptr<DirectX::CommonStates> m_states;
    std::unique_ptr<DirectX::IEffectFactory> m_fxFactory;
    std::unique_ptr<DirectX::Model> m_model;

    // Variables used with the rendering loop.
    bool m_loadingComplete = false;
    bool m_enableCursor = false;

    // If the current D3D Device supports VPRT, we can avoid using a geometry
    // shader just to set the render target array index.
    bool m_usingVprtShaders = false;
  };
}
