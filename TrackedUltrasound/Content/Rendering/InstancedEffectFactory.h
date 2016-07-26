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

#include <Effects.h>

namespace DirectX
{
  // Factory for sharing effects and texture resources
  class InstancedEffectFactory : public IEffectFactory
  {
  public:
    explicit InstancedEffectFactory( _In_ ID3D11Device* device );
    InstancedEffectFactory( InstancedEffectFactory&& moveFrom );
    InstancedEffectFactory& operator= ( InstancedEffectFactory&& moveFrom );

    InstancedEffectFactory( InstancedEffectFactory const& ) = delete;
    InstancedEffectFactory& operator= ( InstancedEffectFactory const& ) = delete;

    virtual ~InstancedEffectFactory();

    // IEffectFactory methods.
    virtual std::shared_ptr<IEffect> __cdecl CreateEffect( _In_ const EffectInfo& info, _In_opt_ ID3D11DeviceContext* deviceContext ) override;
    virtual void __cdecl CreateTexture( _In_z_ const wchar_t* name, _In_opt_ ID3D11DeviceContext* deviceContext, _Outptr_ ID3D11ShaderResourceView** textureView ) override;

    // Settings.
    void __cdecl ReleaseCache();

    void __cdecl SetSharing( bool enabled );

    void __cdecl SetUseNormalMapEffect( bool enabled );

    void __cdecl SetDirectory( _In_opt_z_ const wchar_t* path );

  private:
    // Private implementation.
    class Impl;

    std::shared_ptr<Impl> pImpl;
  };
}