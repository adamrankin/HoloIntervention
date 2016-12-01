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

// DirectXTK includes
#include <Effects.h>

namespace DirectX
{
  // Abstract interface for effects with world, view, and projection matrices.
  class IStereoEffectMatrices
  {
  public:
    virtual ~IStereoEffectMatrices() { }

    virtual void XM_CALLCONV SetWorld(FXMMATRIX value) = 0;
    virtual void XM_CALLCONV SetView(FXMMATRIX value[2]) = 0;
    virtual void XM_CALLCONV SetProjection(FXMMATRIX value[2]) = 0;
    virtual void XM_CALLCONV SetMatrices(FXMMATRIX world, FXMMATRIX view[2], FXMMATRIX projection[2]);
  };

  class InstancedBasicEffect : public IEffect, public IStereoEffectMatrices, public IEffectLights, public IEffectFog
  {
  public:
    explicit InstancedBasicEffect(_In_ ID3D11Device* device);
    InstancedBasicEffect(InstancedBasicEffect&& moveFrom);
    InstancedBasicEffect& operator= (InstancedBasicEffect&& moveFrom);

    InstancedBasicEffect(InstancedBasicEffect const&) = delete;
    InstancedBasicEffect& operator= (InstancedBasicEffect const&) = delete;

    virtual ~InstancedBasicEffect();

    // IEffect methods.
    void __cdecl Apply(_In_ ID3D11DeviceContext* deviceContext) override;

    void __cdecl GetVertexShaderBytecode(_Out_ void const** pShaderByteCode, _Out_ size_t* pByteCodeLength) override;

    // Camera settings.
    void XM_CALLCONV SetWorld(FXMMATRIX value) override;
    void XM_CALLCONV SetView(FXMMATRIX value[2]) override;
    void XM_CALLCONV SetProjection(FXMMATRIX value[2]) override;
    void XM_CALLCONV SetMatrices(FXMMATRIX world, FXMMATRIX view[2], FXMMATRIX projection[2]) override;

    // Material settings.
    void XM_CALLCONV SetDiffuseColor(FXMVECTOR value);
    FXMVECTOR XM_CALLCONV GetDiffuseColor() const;
    void XM_CALLCONV SetEmissiveColor(FXMVECTOR value);
    FXMVECTOR XM_CALLCONV GetEmissiveColor() const;
    void XM_CALLCONV SetSpecularColor(FXMVECTOR value);
    void __cdecl SetSpecularPower(float value);
    void __cdecl DisableSpecular();
    void __cdecl SetAlpha(float value);
    float __cdecl GetAlpha() const;
    void XM_CALLCONV SetColorAndAlpha(FXMVECTOR value);

    // Light settings.
    void __cdecl SetLightingEnabled(bool value) override;
    void __cdecl SetPerPixelLighting(bool value) override;
    void XM_CALLCONV SetAmbientLightColor(FXMVECTOR value) override;

    void __cdecl SetLightEnabled(int whichLight, bool value) override;
    void XM_CALLCONV SetLightDirection(int whichLight, FXMVECTOR value) override;
    void XM_CALLCONV SetLightDiffuseColor(int whichLight, FXMVECTOR value) override;
    void XM_CALLCONV SetLightSpecularColor(int whichLight, FXMVECTOR value) override;

    void __cdecl EnableDefaultLighting() override;

    // Fog settings.
    void __cdecl SetFogEnabled(bool value) override;
    void __cdecl SetFogStart(float value) override;
    void __cdecl SetFogEnd(float value) override;
    void XM_CALLCONV SetFogColor(FXMVECTOR value) override;

    // Vertex color setting.
    void __cdecl SetVertexColorEnabled(bool value);

    // Texture setting.
    void __cdecl SetTextureEnabled(bool value);
    void __cdecl SetTexture(_In_opt_ ID3D11ShaderResourceView* value);

  private:
    // Private implementation.
    class Impl;

    std::unique_ptr<Impl> pImpl;
  };
}