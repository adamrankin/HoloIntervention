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
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::ApplicationModel;
using namespace Windows::Foundation;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Storage;
using namespace Windows::UI::Core;

//----------------------------------------------------------------------------
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
  IFrameworkView^ AppViewSource::CreateView()
  {
    return ref new AppView();
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
    CoreApplication::LeavingBackground +=
      ref new EventHandler<LeavingBackgroundEventArgs^>(this, &AppView::OnLeavingBackground);

    m_deviceResources = std::make_shared<DX::DeviceResources>();

    m_main = std::make_unique<HoloInterventionCore>(m_deviceResources);
  }

  //----------------------------------------------------------------------------
  // Called when the CoreWindow object is created (or re-created).
  void AppView::SetWindow(CoreWindow^ window)
  {
    window->KeyDown +=
      ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &AppView::OnKeyPressed);
    window->Closed +=
      ref new TypedEventHandler<CoreWindow^, CoreWindowEventArgs^>(this, &AppView::OnWindowClosed);
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
  void AppView::OnViewActivated(CoreApplicationView^ sender, IActivatedEventArgs^ args)
  {
    sender->CoreWindow->Activate();
  }

  //----------------------------------------------------------------------------
  void AppView::OnSuspending(Platform::Object^ sender, SuspendingEventArgs^ args)
  {
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
          LOG(LogLevelType::LOG_LEVEL_WARNING, std::string("Unable to save app state: ") + e.what());
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
      catch (const std::exception& e) { OutputDebugStringA(e.what()); }
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

  //----------------------------------------------------------------------------
  void AppView::OnLeavingBackground(Platform::Object^ sender, Windows::ApplicationModel::LeavingBackgroundEventArgs^ args)
  {
    if (m_main != nullptr)
    {
      try
      {
        m_main->LoadAppStateAsync();
      }
      catch (const std::exception& e) { OutputDebugStringA(e.what()); }
    }
  }
}