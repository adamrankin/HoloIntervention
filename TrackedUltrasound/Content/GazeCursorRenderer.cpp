#include "pch.h"
#include "GazeCursorRenderer.h"
#include "Common\DirectXHelper.h"

// DirectXTK includes
#include <Model.h>
#include <SimpleMath.h>

using namespace Concurrency;
using namespace DirectX::SimpleMath;
using namespace DirectX;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Foundation;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::UI::Input::Spatial;

namespace TrackedUltrasound
{
  //----------------------------------------------------------------------------
  // Loads vertex and pixel shaders from files and instantiates the cube geometry.
  GazeCursorRenderer::GazeCursorRenderer( const std::shared_ptr<DX::DeviceResources>& deviceResources )
    : m_deviceResources( deviceResources )
    , m_states ( nullptr )
    , m_fxFactory ( nullptr )
    , m_model ( nullptr )
  {
    CreateDeviceDependentResources();
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::Update( const DX::StepTimer& timer, SpatialPointerPose^ pointerPose )
  {
    if ( !m_enableCursor )
    {
      // No need to update, cursor is not drawn
      return;
    }

    // TODO : write new logic to calculate intersection location and normal orientation of intersected surface
    // and set that as the models internal coordinate system (m_world)

    /* -------------- pointer calculation ref code ------------
    if (pointerPose != nullptr)
    {
      // Get the gaze direction relative to the given coordinate system.
      const float3 headPosition = pointerPose->Head->Position;
      const float3 headDirection = pointerPose->Head->ForwardDirection;

      // The hologram is positioned two meters along the user's gaze direction.
      static const float distanceFromUser = 2.0f; // meters
      const float3 gazeAtTwoMeters = headPosition + (distanceFromUser * headDirection);

      // This will be used as the translation component of the hologram's
      // model transform.
      SetPosition(gazeAtTwoMeters);
    }
    ----------- end reference code --------------- */

    /* -------------- Code kept as reference ------------
    // Rotate the cube.
    // Convert degrees to radians, then convert seconds to rotation angle.
    const float    radiansPerSecond = XMConvertToRadians( m_degreesPerSecond );
    const double   totalRotation = timer.GetTotalSeconds() * radiansPerSecond;
    const float    radians = static_cast<float>( fmod( totalRotation, XM_2PI ) );
    const XMMATRIX modelRotation = XMMatrixRotationY( -radians );

    // Position the cube.
    const XMMATRIX modelTranslation = XMMatrixTranslationFromVector( XMLoadFloat3( &m_position ) );

    // Multiply to get the transform matrix.
    // Note that this transform does not enforce a particular coordinate system. The calling
    // class is responsible for rendering this content in a consistent manner.
    const XMMATRIX modelTransform = XMMatrixMultiply( modelRotation, modelTranslation );

    // The view and projection matrices are provided by the system; they are associated
    // with holographic cameras, and updated on a per-camera basis.
    // Here, we provide the model transform for the sample hologram. The model transform
    // matrix is transposed to prepare it for the shader.
    XMStoreFloat4x4( &m_modelConstantBufferData.model, XMMatrixTranspose( modelTransform ) );

    // Loading is asynchronous. Resources must be created before they can be updated.
    if ( !m_loadingComplete )
    {
      return;
    }

    // Use the D3D device context to update Direct3D device-based resources.
    const auto context = m_deviceResources->GetD3DDeviceContext();

    // Update the model transform buffer for the hologram.
    context->UpdateSubresource(
      m_modelConstantBuffer.Get(),
      0,
      nullptr,
      &m_modelConstantBufferData,
      0,
      0
    );
    ----------- end reference code --------------- */
  }

  //----------------------------------------------------------------------------
  // Renders one frame using the vertex and pixel shaders.
  // On devices that do not support the D3D11_FEATURE_D3D11_OPTIONS3::
  // VPAndRTArrayIndexFromAnyShaderFeedingRasterizer optional feature,
  // a pass-through geometry shader is also used to set the render
  // target array index.
  void GazeCursorRenderer::Render()
  {
    // Loading is asynchronous. Resources must be created before drawing can occur.
    if ( !m_loadingComplete || !m_enableCursor )
    {
      return;
    }

    const auto context = m_deviceResources->GetD3DDeviceContext();

    // TODO : Copy the view and projection matrices from the device resources

    m_model->Draw( context, *m_states, Matrix::Identity, Matrix::Identity, Matrix::Identity );
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::EnableCursor( bool enable )
  {
    m_enableCursor = enable;
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::ToggleCursor()
  {
    m_enableCursor = !m_enableCursor;
  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::CreateDeviceDependentResources()
  {
    m_usingVprtShaders = m_deviceResources->GetDeviceSupportsVprt();

    std::shared_ptr<std::vector<uint8_t>> meshDataVector = std::make_shared<std::vector<uint8_t>>();

    auto loadModelDataTask = task<void>( [meshDataVector]()
    {
      StorageFolder^ folder = Windows::ApplicationModel::Package::Current->InstalledLocation;
      auto task = create_task(folder->GetFileAsync(L"Assets\model.cmo"));
      auto storageFile = task.get();

      auto readAsyncTask = create_task( FileIO::ReadBufferAsync( storageFile ) );

      auto buffer = readAsyncTask.get();
      DataReader^ reader = DataReader::FromBuffer( buffer );
      meshDataVector->resize( reader->UnconsumedBufferLength );

      if ( !meshDataVector->empty() )
      {
        reader->ReadBytes( ::Platform::ArrayReference<unsigned char>( &( *meshDataVector )[0], meshDataVector->size() ) );
      }
    } );

    auto createModelTask = loadModelDataTask.then( [&]( task<void> previousTask )
    {
      previousTask.wait();

      auto device = m_deviceResources->GetD3DDevice();
      bool result( true );

      m_states = std::make_unique<DirectX::CommonStates>( device );
      m_fxFactory = std::make_unique<DirectX::EffectFactory>( device );
      m_model = Model::CreateFromCMO( device, &meshDataVector->front(), meshDataVector->size(), *m_fxFactory );
    } );

    // Once the cube is loaded, the object is ready to be rendered.
    createModelTask.then( [&]( task<void> previousTask )
    {
      try
      {
        previousTask.get();
        m_loadingComplete = true;
      }
      catch ( const std::exception& e )
      {
        OutputDebugStringA( e.what() );
        m_loadingComplete = false;
      }
    } );


  }

  //----------------------------------------------------------------------------
  void GazeCursorRenderer::ReleaseDeviceDependentResources()
  {
    m_loadingComplete = false;
    m_usingVprtShaders = false;
  }

}