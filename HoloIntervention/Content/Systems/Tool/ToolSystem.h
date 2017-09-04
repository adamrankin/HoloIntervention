/*====================================================================
Copyright(c) 2017 Adam Rankin


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
#include "IConfigurable.h"
#include "IStabilizedComponent.h"
#include "IVoiceInput.h"

namespace HoloIntervention
{
  namespace Rendering
  {
    class ModelRenderer;
  }

  class TransformName;

  namespace Tools
  {
    class ToolEntry;
  }

  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;

    class ToolSystem : public Input::IVoiceInput, public IStabilizedComponent, public IConfigurable
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> WriteConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> ReadConfigurationAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      ToolSystem(NotificationSystem& notificationSystem,
                 RegistrationSystem& registrationSystem,
                 Rendering::ModelRenderer& modelRenderer,
                 NetworkSystem& networkSystem);
      ~ToolSystem();

      uint32 GetToolCount() const;
      std::shared_ptr<Tools::ToolEntry> GetTool(uint64 token) const;
      std::vector<std::shared_ptr<Tools::ToolEntry>> GetTools();
      bool IsToolValid(uint64 token) const;
      bool WasToolValid(uint64 token) const;

      Concurrency::task<uint64> RegisterToolAsync(const std::wstring& modelName, UWPOpenIGTLink::TransformName^ coordinateFrame);
      void UnregisterTool(uint64 toolToken);
      void ClearTools();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(Input::VoiceInputCallbackMap& callbackMap);

    protected:
      Concurrency::task<void> InitAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    protected:
      // Cached entries
      NotificationSystem&                               m_notificationSystem;
      RegistrationSystem&                               m_registrationSystem;
      NetworkSystem&                                    m_networkSystem;
      Rendering::ModelRenderer&                         m_modelRenderer;

      std::wstring                                      m_connectionName; // For config saving
      uint64                                            m_hashedConnectionName;
      double                                            m_latestTimestamp;
      mutable std::mutex                                m_entriesMutex;
      std::vector<std::shared_ptr<Tools::ToolEntry>>    m_toolEntries;
      UWPOpenIGTLink::TransformRepository^              m_transformRepository;
    };
  }
}