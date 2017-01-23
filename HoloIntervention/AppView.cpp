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
#include "AppView.h"
#include "HoloInterventionCore.h"

// Common includes
#include "DeviceResources.h"

// Windows includes
#include <ppltasks.h>

// System includes
#include "NotificationSystem.h"

using namespace Concurrency;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Holographic;
using namespace Windows::UI::Core;

//----------------------------------------------------------------------------
// The main function is only used to initialize our IFrameworkView class.
// Under most circumstances, you should not need to modify this function.
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
  HoloIntervention::AppViewSource^ appViewSource = ref new HoloIntervention::AppViewSource();
  CoreApplication::Run(appViewSource);
  return 0;
}

namespace HoloIntervention
{
  //----------------------------------------------------------------------------
  HoloIntervention::AppView^ instance()
  {
    static AppView^ ins = ref new AppView();
    return ins;
  }

  //----------------------------------------------------------------------------
  IFrameworkView^ AppViewSource::CreateView()
  {
    return HoloIntervention::instance();
  }

  //----------------------------------------------------------------------------
  AppView::AppView()
  {
  }

  //----------------------------------------------------------------------------
  void AppView::Initialize(CoreApplicationView^ applicationView)
  {
    applicationView->Activated +=
      ref new TypedEventHandler<CoreApplicationView^, IActivatedEventArgs^>(this, &AppView::OnViewActivated);

    // Register event handlers for app lifecycle.
    CoreApplication::Suspending +=
      ref new EventHandler<SuspendingEventArgs^>(this, &AppView::OnSuspending);

    CoreApplication::Resuming +=
      ref new EventHandler<Platform::Object^>(this, &AppView::OnResuming);

    // At this point we have access to the device and we can create device-dependent
    // resources.
    m_deviceResources = std::make_shared<DX::DeviceResources>();

    m_main = std::make_unique<HoloInterventionCore>(m_deviceResources);
  }

  // Called when the CoreWindow object is created (or re-created).
  void AppView::SetWindow(CoreWindow^ window)
  {
    // Register for keypress notifications.
    window->KeyDown +=
      ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &AppView::OnKeyPressed);

    // Register for notification that the app window is being closed.
    window->Closed +=
      ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &AppView::OnWindowClosed);

    // Register for notifications that the app window is losing focus.
    window->VisibilityChanged +=
      ref new TypedEventHandler<CoreWindow^, VisibilityChangedEventArgs^>(this, &AppView::OnVisibilityChanged);

    m_holographicSpace = HolographicSpace::CreateForCoreWindow(window);
    m_deviceResources->SetHolographicSpace(m_holographicSpace);
    m_main->SetHolographicSpace(m_holographicSpace);
  }

  //----------------------------------------------------------------------------
  void AppView::Load(Platform::String^ entryPoint)
  {
  }

  //----------------------------------------------------------------------------
  void AppView::Run()
  {
    while (!m_windowClosed)
    {
      if (m_windowVisible && (m_holographicSpace != nullptr))
      {
        CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

        HolographicFrame^ holographicFrame = m_main->Update();

        if (m_main->Render(holographicFrame))
        {
          // The holographic frame has an API that presents the swap chain for each
          // holographic camera.
          m_deviceResources->Present(holographicFrame);
        }
      }
      else
      {
        CoreWindow::GetForCurrentThread()->Dispatcher->ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
      }
    }
  }

  //----------------------------------------------------------------------------
  void AppView::Uninitialize()
  {

  }

  //----------------------------------------------------------------------------
  System::NotificationSystem& AppView::GetNotificationSystem()
  {
    return m_main->GetNotificationsSystem();
  }

  //----------------------------------------------------------------------------
  System::SpatialSystem& AppView::GetSpatialSystem()
  {
    return m_main->GetSpatialSystem();
  }

  //----------------------------------------------------------------------------
  System::GazeSystem& AppView::GetGazeSystem()
  {
    return m_main->GetGazeSystem();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::System::IconSystem& AppView::GetIconSystem()
  {
    return m_main->GetIconSystem();
  }

  //----------------------------------------------------------------------------
  System::RegistrationSystem& AppView::GetRegistrationSystem()
  {
    return m_main->GetRegistrationSystem();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::System::ToolSystem& AppView::GetToolSystem()
  {
    return m_main->GetToolSystem();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::System::ImagingSystem& AppView::GetImagingSystem()
  {
    return m_main->GetImagingSystem();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::Sound::SoundManager& AppView::GetSoundManager()
  {
    return m_main->GetSoundManager();
  }

  //----------------------------------------------------------------------------
  HoloIntervention::Network::IGTLinkIF& AppView::GetIGTLink()
  {
    return m_main->GetIGTLink();
  }

  //----------------------------------------------------------------------------
  Rendering::ModelRenderer& AppView::GetModelRenderer()
  {
    return m_main->GetModelRenderer();
  }

  //----------------------------------------------------------------------------
  Rendering::SliceRenderer& AppView::GetSliceRenderer()
  {
    return m_main->GetSliceRenderer();
  }

  //----------------------------------------------------------------------------
  Rendering::VolumeRenderer& AppView::GetVolumeRenderer()
  {
    return m_main->GetVolumeRenderer();
  }

  //----------------------------------------------------------------------------
  uint64 AppView::GetCurrentFrameNumber() const
  {
    return m_main->GetCurrentFrameNumber();
  }

  //----------------------------------------------------------------------------
  void AppView::OnViewActivated(CoreApplicationView^ sender, IActivatedEventArgs^ args)
  {
    // Run() won't start until the CoreWindow is activated.
    sender->CoreWindow->Activate();
  }

  //----------------------------------------------------------------------------
  void AppView::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
  {
    // Save app state asynchronously after requesting a deferral. Holding a deferral
    // indicates that the application is busy performing suspending operations. Be
    // aware that a deferral may not be held indefinitely; after about five seconds,
    // the app will be forced to exit.
    SuspendingDeferral^ deferral = args->SuspendingOperation->GetDeferral();

    create_task([this, deferral]()
    {
      m_deviceResources->Trim();

      if (m_main != nullptr)
      {
        try
        {
          m_main->SaveAppStateAsync();
        }
        catch (const std::exception& e)
        {
          HoloIntervention::Log::instance().LogMessage(Log::LOG_LEVEL_WARNING, e.what());
        }
      }

      deferral->Complete();
    });
  }

  //----------------------------------------------------------------------------
  void AppView::OnResuming(Platform::Object^ sender, Platform::Object^ args)
  {
    if (m_main != nullptr)
    {
      try
      {
        m_main->LoadAppStateAsync();
      }
      catch (const std::exception&)
      {
        m_main->GetNotificationsSystem().QueueMessage(L"Unable to load spatial anchor store. Please re-place anchors.");
      }
    }
  }

  //----------------------------------------------------------------------------
  void AppView::OnVisibilityChanged(CoreWindow^ sender, VisibilityChangedEventArgs^ args)
  {
    m_windowVisible = args->Visible;
  }

  //----------------------------------------------------------------------------
  void AppView::OnWindowClosed(CoreWindow^ sender, CoreWindowEventArgs^ args)
  {
    m_windowClosed = true;
  }

  //----------------------------------------------------------------------------
  void AppView::OnKeyPressed(CoreWindow^ sender, KeyEventArgs^ args)
  {
    // TODO: Bluetooth keyboards are supported by HoloLens.
  }
}