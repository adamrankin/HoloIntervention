/*====================================================================
Copyright(c) 2018 Adam Rankin


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

// Valhalla includes
#include <Input\IVoiceInput.h>
#include <Interfaces\ISerializable.h>
#include <Interfaces\IStabilizedComponent.h>

namespace Valhalla
{
  namespace Rendering
  {
    class ModelRenderer;
  }

  class TransformName;

  namespace Tools
  {
    class Tool;
  }

  namespace UI
  {
    class Icons;
  }
}

namespace HoloIntervention
{
  namespace System
  {
    class NetworkSystem;
    class NotificationSystem;
    class RegistrationSystem;

    class ToolSystem : public Valhalla::Input::IVoiceInput, public Valhalla::IStabilizedComponent, public Valhalla::ISerializable
    {
    public:
      virtual Windows::Foundation::Numerics::float3 GetStabilizedPosition(Windows::UI::Input::Spatial::SpatialPointerPose^ pose) const;
      virtual Windows::Foundation::Numerics::float3 GetStabilizedVelocity() const;
      virtual float GetStabilizePriority() const;

    public:
      virtual concurrency::task<bool> SaveAsync(Windows::Data::Xml::Dom::XmlDocument^ document);
      virtual concurrency::task<bool> LoadAsync(Windows::Data::Xml::Dom::XmlDocument^ document);

    public:
      ToolSystem(Valhalla::ValhallaCore& core,
                 NotificationSystem& notificationSystem,
                 RegistrationSystem& registrationSystem,
                 Valhalla::Rendering::ModelRenderer& modelRenderer,
                 NetworkSystem& networkSystem,
                 Valhalla::UI::Icons& icons);
      ~ToolSystem();

      uint32 GetToolCount() const;
      std::shared_ptr<Tools::Tool> GetTool(uint64 token) const;
      std::shared_ptr<Tools::Tool> GetToolByUserId(const std::wstring& userId) const;
      std::shared_ptr<Tools::Tool> GetToolByUserId(Platform::String^ userId) const;
      std::vector<std::shared_ptr<Tools::Tool>> GetTools();
      bool IsToolValid(uint64 token) const;
      bool WasToolValid(uint64 token) const;

      Concurrency::task<uint64> RegisterToolAsync(const std::wstring& modelName, Platform::String^ userId, Windows::Foundation::Numerics::float4x4 modelToObjectTransform, const bool isPrimitive, UWPOpenIGTLink::TransformName^ coordinateFrame, Windows::Foundation::Numerics::float4 colour = Windows::Foundation::Numerics::float4::one(), Windows::Foundation::Numerics::float3 argument = Windows::Foundation::Numerics::float3::one(), size_t tessellation = 16, bool rhcoords = true, bool invertn = false);
      void UnregisterTool(uint64 toolToken);
      void ClearTools();

      void Update(const DX::StepTimer& timer, Windows::Perception::Spatial::SpatialCoordinateSystem^ coordSystem);

      // IVoiceInput functions
      virtual void RegisterVoiceCallbacks(Valhalla::Input::VoiceInputCallbackMap& callbackMap);

      void ShowIcons(bool show);

    protected:
      // Cached entries
      NotificationSystem&                                 m_notificationSystem;
      RegistrationSystem&                                 m_registrationSystem;
      NetworkSystem&                                      m_networkSystem;
      Valhalla::UI::Icons&                                m_icons;
      Valhalla::Rendering::ModelRenderer&                 m_modelRenderer;

      std::wstring                                        m_connectionName; // For config saving
      uint64                                              m_hashedConnectionName;
      bool                                                m_showIcons = false;
      double                                              m_latestTimestamp;
      mutable std::mutex                                  m_entriesMutex;
      std::vector<std::shared_ptr<Tools::Tool>>           m_tools;
      UWPOpenIGTLink::TransformRepository^                m_transformRepository;
    };
  }
}