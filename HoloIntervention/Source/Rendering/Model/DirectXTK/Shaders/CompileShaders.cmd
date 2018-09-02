@echo off
rem THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
rem ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
rem THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
rem PARTICULAR PURPOSE.
rem
rem Copyright (c) Microsoft Corporation. All rights reserved.
rem
rem Modifications:
rem   August 2016 - Adam Rankin, Robarts Research Institute
rem     - Refactoring to create instanced shaders with appropriately named variables
rem     - Support for basic effect only
rem   September 2017 - Adam Rankin, Robarts Research Institute
rem     - Readding support for instanced DGSL shaders

setlocal
set error=0

mkdir Compiled

if %1.==vprt. goto continue
if %1.==. goto continue
echo usage: CompileShaders [xbox] | [vprt]
exit /b

:continue

rem call :CompileShader%1 AlphaTestEffect vs VSAlphaTest
rem call :CompileShader%1 AlphaTestEffect vs VSAlphaTestNoFog
rem call :CompileShader%1 AlphaTestEffect vs VSAlphaTestVc
rem call :CompileShader%1 AlphaTestEffect vs VSAlphaTestVcNoFog
rem 
rem call :CompileShader%1 AlphaTestEffect ps PSAlphaTestLtGt
rem call :CompileShader%1 AlphaTestEffect ps PSAlphaTestLtGtNoFog
rem call :CompileShader%1 AlphaTestEffect ps PSAlphaTestEqNe
rem call :CompileShader%1 AlphaTestEffect ps PSAlphaTestEqNeNoFog

call :CompileShader%1 InstancedBasicEffect vs VSBasic
call :CompileShader%1 InstancedBasicEffect vs VSBasicNoFog
call :CompileShader%1 InstancedBasicEffect vs VSBasicVc
call :CompileShader%1 InstancedBasicEffect vs VSBasicVcNoFog
call :CompileShader%1 InstancedBasicEffect vs VSBasicTx
call :CompileShader%1 InstancedBasicEffect vs VSBasicTxNoFog
call :CompileShader%1 InstancedBasicEffect vs VSBasicTxVc
call :CompileShader%1 InstancedBasicEffect vs VSBasicTxVcNoFog

call :CompileShader%1 InstancedBasicEffect vs VSBasicVertexLighting
call :CompileShader%1 InstancedBasicEffect vs VSBasicVertexLightingVc
call :CompileShader%1 InstancedBasicEffect vs VSBasicVertexLightingTx
call :CompileShader%1 InstancedBasicEffect vs VSBasicVertexLightingTxVc

call :CompileShader%1 InstancedBasicEffect vs VSBasicOneLight
call :CompileShader%1 InstancedBasicEffect vs VSBasicOneLightVc
call :CompileShader%1 InstancedBasicEffect vs VSBasicOneLightTx
call :CompileShader%1 InstancedBasicEffect vs VSBasicOneLightTxVc

call :CompileShader%1 InstancedBasicEffect vs VSBasicPixelLighting
call :CompileShader%1 InstancedBasicEffect vs VSBasicPixelLightingVc
call :CompileShader%1 InstancedBasicEffect vs VSBasicPixelLightingTx
call :CompileShader%1 InstancedBasicEffect vs VSBasicPixelLightingTxVc

if %1.==vprt. goto end

call :CompileShader%1 InstancedBasicEffect ps PSBasic
call :CompileShader%1 InstancedBasicEffect ps PSBasicNoFog
call :CompileShader%1 InstancedBasicEffect ps PSBasicTx
call :CompileShader%1 InstancedBasicEffect ps PSBasicTxNoFog

call :CompileShader%1 InstancedBasicEffect ps PSBasicVertexLighting
call :CompileShader%1 InstancedBasicEffect ps PSBasicVertexLightingNoFog
call :CompileShader%1 InstancedBasicEffect ps PSBasicVertexLightingTx
call :CompileShader%1 InstancedBasicEffect ps PSBasicVertexLightingTxNoFog

call :CompileShader%1 InstancedBasicEffect ps PSBasicPixelLighting
call :CompileShader%1 InstancedBasicEffect ps PSBasicPixelLightingTx

rem call :CompileShader%1 DualTextureEffect vs VSDualTexture
rem call :CompileShader%1 DualTextureEffect vs VSDualTextureNoFog
rem call :CompileShader%1 DualTextureEffect vs VSDualTextureVc
rem call :CompileShader%1 DualTextureEffect vs VSDualTextureVcNoFog
rem 
rem call :CompileShader%1 DualTextureEffect ps PSDualTexture
rem call :CompileShader%1 DualTextureEffect ps PSDualTextureNoFog
rem 
rem call :CompileShader%1 EnvironmentMapEffect vs VSEnvMap
rem call :CompileShader%1 EnvironmentMapEffect vs VSEnvMapFresnel
rem call :CompileShader%1 EnvironmentMapEffect vs VSEnvMapOneLight
rem call :CompileShader%1 EnvironmentMapEffect vs VSEnvMapOneLightFresnel
rem call :CompileShader%1 EnvironmentMapEffect vs VSEnvMapPixelLighting
rem 
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMap
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapNoFog
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapSpecular
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapSpecularNoFog
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapPixelLighting
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapPixelLightingNoFog
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapPixelLightingFresnel
rem call :CompileShader%1 EnvironmentMapEffect ps PSEnvMapPixelLightingFresnelNoFog
rem 
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedVertexLightingOneBone
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedVertexLightingTwoBones
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedVertexLightingFourBones
rem 
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedOneLightOneBone
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedOneLightTwoBones
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedOneLightFourBones
rem 
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedPixelLightingOneBone
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedPixelLightingTwoBones
rem call :CompileShader%1 SkinnedEffect vs VSSkinnedPixelLightingFourBones
rem 
rem call :CompileShader%1 SkinnedEffect ps PSSkinnedVertexLighting
rem call :CompileShader%1 SkinnedEffect ps PSSkinnedVertexLightingNoFog
rem call :CompileShader%1 SkinnedEffect ps PSSkinnedPixelLighting
rem 
rem call :CompileShader%1 NormalMapEffect vs VSNormalPixelLightingTx
rem call :CompileShader%1 NormalMapEffect vs VSNormalPixelLightingTxVc
rem 
rem call :CompileShader%1 NormalMapEffect ps PSNormalPixelLightingTx
rem call :CompileShader%1 NormalMapEffect ps PSNormalPixelLightingTxNoFog
rem call :CompileShader%1 NormalMapEffect ps PSNormalPixelLightingTxNoSpec
rem call :CompileShader%1 NormalMapEffect ps PSNormalPixelLightingTxNoFogSpec
rem 
rem call :CompileShader%1 SpriteEffect vs SpriteVertexShader
rem call :CompileShader%1 SpriteEffect ps SpritePixelShader
rem 
echo.

:end

if %error% == 0 (
    echo Shaders compiled ok
) else (
    echo There were shader compilation errors!
)

endlocal
exit /b

:CompileShader
rem /Qstrip_debug temporarily removed during development
set fxc=fxc /nologo %1.fx /T%2_5_0 /Zi /Zpc /Qstrip_reflect /E%3 /FhCompiled\%1_%3.inc /FdCompiled\%1_%3.pdb /Vn%1_%3
echo.
echo %fxc%
%fxc% || set error=1
exit /b

:CompileShaderHLSL
rem /Qstrip_debug temporarily removed during development
set fxc=fxc /nologo %1.hlsl /T%2_5_0 /Zi /Zpc /Qstrip_reflect /E%3 /FhCompiled\%1_%3.inc /FdCompiled\%1_%3.pdb /Vn%1_%3
echo.
echo %fxc%
%fxc% || set error=1
exit /b

:CompileShadervprt
rem /Qstrip_debug temporarily removed during development
set fxc=fxc /nologo %1.fx /T%2_5_0 /Zi /Zpc /Qstrip_reflect /E%3 /FhCompiled\%1_%3_VPRT.inc /FdCompiled\%1_%3_VPRT.pdb /Vn%1_%3VPRT /DUSE_VPRT
echo.
echo %fxc%
%fxc% || set error=1
exit /b

:CompileShaderHLSLvprt
rem /Qstrip_debug temporarily removed during development
set fxc=fxc /nologo %1.hlsl /T%2_5_0 /Zi /Zpc /Qstrip_reflect /E%3 /FhCompiled\%1_%3_VPRT.inc /FdCompiled\%1_%3_VPRT.pdb /Vn%1_%3VPRT /DUSE_VPRT
echo.
echo %fxc%
%fxc% || set error=1
exit /b