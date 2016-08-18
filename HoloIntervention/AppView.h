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
#include "DeviceResources.h"
#include "HoloInterventionMain.h"

namespace HoloIntervention
{
  namespace Notifications
  {
    class NotificationSystem;
  }

  // IFrameworkView class. Connects the app with the Windows shell and handles application lifecycle events.
  ref class AppView sealed : public Windows::ApplicationModel::Core::IFrameworkView
  {
  public:
    AppView();

    // IFrameworkView methods.
    virtual void Initialize( Windows::ApplicationModel::Core::CoreApplicationView ^ applicationView );
    virtual void SetWindow( Windows::UI::Core::CoreWindow ^ window );
    virtual void Load( Platform::String ^ entryPoint );
    virtual void Run();
    virtual void Uninitialize();

  internal:
    // Provide app wide access to the notifications
    Notifications::NotificationSystem& GetNotificationAPI();

  protected:
    // Application lifecycle event handlers.
    void OnViewActivated( Windows::ApplicationModel::Core::CoreApplicationView ^ sender, Windows::ApplicationModel::Activation::IActivatedEventArgs ^ args );
    void OnSuspending( Platform::Object ^ sender, Windows::ApplicationModel::SuspendingEventArgs ^ args );
    void OnResuming( Platform::Object ^ sender, Platform::Object ^ args );

    // Window event handlers.
    void OnVisibilityChanged( Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::VisibilityChangedEventArgs ^ args );
    void OnWindowClosed( Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::CoreWindowEventArgs ^ args );

    // CoreWindow input event handlers.
    void OnKeyPressed( Windows::UI::Core::CoreWindow ^ sender, Windows::UI::Core::KeyEventArgs ^ args );

  private:
    std::unique_ptr<HoloInterventionMain> m_main;

    std::shared_ptr<DX::DeviceResources> m_deviceResources;
    bool m_windowClosed  = false;
    bool m_windowVisible = true;

    // The holographic space the app will use for rendering.
    Windows::Graphics::Holographic::HolographicSpace ^ m_holographicSpace = nullptr;
  };

  static AppView^ instance()
  {
    static AppView^ ins = ref new AppView();
    return ins;
  }

  // The entry point for the app.
  ref class AppViewSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
  {
  public:
    virtual Windows::ApplicationModel::Core::IFrameworkView ^ CreateView();
  };
}

