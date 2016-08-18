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
#include "InstancedEffectFactory.h"

// Effect type includes
#include "InstancedBasicEffect.h"

// DirectXTK Includes
#include <Effects.h>
#include <DemandCreate.h>
#include <SharedResourcePool.h>

#include <DDSTextureLoader.h>
#include <WICTextureLoader.h>

using Microsoft::WRL::ComPtr;

namespace DirectX
{
  // Internal EffectFactory implementation class. Only one of these helpers is allocated
  // per D3D device, even if there are multiple public facing EffectFactory instances.
  class InstancedEffectFactory::Impl
  {
  public:
    Impl( _In_ ID3D11Device* device )
      : device( device ),
        mSharing( true ),
        mUseNormalMapEffect( true )
    {
      *mPath = 0;
    }

    std::shared_ptr<IEffect> CreateEffect( _In_ IEffectFactory* factory, _In_ const IEffectFactory::EffectInfo& info, _In_opt_ ID3D11DeviceContext* deviceContext );
    void CreateTexture( _In_z_ const wchar_t* texture, _In_opt_ ID3D11DeviceContext* deviceContext, _Outptr_ ID3D11ShaderResourceView** textureView );

    void ReleaseCache();
    void SetSharing( bool enabled ) { mSharing = enabled; }
    void SetUseNormalMapEffect( bool enabled ) { mUseNormalMapEffect = enabled; }

    static SharedResourcePool<ID3D11Device*, Impl> instancePool;

    wchar_t mPath[MAX_PATH];

  private:
    ComPtr<ID3D11Device> device;

    typedef std::map< std::wstring, std::shared_ptr<IEffect> > EffectCache;
    typedef std::map< std::wstring, ComPtr<ID3D11ShaderResourceView> > TextureCache;

    EffectCache  mEffectCache;
    EffectCache  mEffectCacheSkinning;
    EffectCache  mEffectCacheDualTexture;
    EffectCache  mEffectNormalMap;
    TextureCache mTextureCache;

    bool mSharing;
    bool mUseNormalMapEffect;

    std::mutex mutex;
  };

  // Global instance pool.
  SharedResourcePool<ID3D11Device*, InstancedEffectFactory::Impl> InstancedEffectFactory::Impl::instancePool;

  _Use_decl_annotations_
  std::shared_ptr<IEffect> InstancedEffectFactory::Impl::CreateEffect( IEffectFactory* factory, const IEffectFactory::EffectInfo& info, ID3D11DeviceContext* deviceContext )
  {
    // For now, only the basic effect is implemented as an instanced renderer, and only one subset at that
    /*
    if ( info.enableSkinning )
    {
      // SkinnedEffect
      if ( mSharing && info.name && *info.name )
      {
        auto it = mEffectCacheSkinning.find( info.name );
        if ( mSharing && it != mEffectCacheSkinning.end() )
        {
          return it->second;
        }
      }

      auto effect = std::make_shared<SkinnedEffect>( device.Get() );

      effect->EnableDefaultLighting();

      effect->SetAlpha( info.alpha );

      // Skinned Effect does not have an ambient material color, or per-vertex color support

      XMVECTOR color = XMLoadFloat3( &info.diffuseColor );
      effect->SetDiffuseColor( color );

      if ( info.specularColor.x != 0 || info.specularColor.y != 0 || info.specularColor.z != 0 )
      {
        color = XMLoadFloat3( &info.specularColor );
        effect->SetSpecularColor( color );
        effect->SetSpecularPower( info.specularPower );
      }
      else
      {
        effect->DisableSpecular();
      }

      if ( info.emissiveColor.x != 0 || info.emissiveColor.y != 0 || info.emissiveColor.z != 0 )
      {
        color = XMLoadFloat3( &info.emissiveColor );
        effect->SetEmissiveColor( color );
      }

      if ( info.diffuseTexture && *info.diffuseTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.diffuseTexture, deviceContext, &srv );

        effect->SetTexture( srv.Get() );
      }

      if ( mSharing && info.name && *info.name )
      {
        std::lock_guard<std::mutex> lock( mutex );
        mEffectCacheSkinning.insert( EffectCache::value_type( info.name, effect ) );
      }

      return effect;
    }
    else if ( info.enableDualTexture )
    {
      // DualTextureEffect
      if ( mSharing && info.name && *info.name )
      {
        auto it = mEffectCacheDualTexture.find( info.name );
        if ( mSharing && it != mEffectCacheDualTexture.end() )
        {
          return it->second;
        }
      }

      auto effect = std::make_shared<DualTextureEffect>( device.Get() );

      // Dual texture effect doesn't support lighting (usually it's lightmaps)

      effect->SetAlpha( info.alpha );

      if ( info.perVertexColor )
      {
        effect->SetVertexColorEnabled( true );
      }

      XMVECTOR color = XMLoadFloat3( &info.diffuseColor );
      effect->SetDiffuseColor( color );

      if ( info.diffuseTexture && *info.diffuseTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.diffuseTexture, deviceContext, &srv );

        effect->SetTexture( srv.Get() );
      }

      if ( info.specularTexture && *info.specularTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.specularTexture, deviceContext, &srv );

        effect->SetTexture2( srv.Get() );
      }

      if ( mSharing && info.name && *info.name )
      {
        std::lock_guard<std::mutex> lock( mutex );
        mEffectCacheDualTexture.insert( EffectCache::value_type( info.name, effect ) );
      }

      return effect;
    }
    else if ( info.enableNormalMaps && mUseNormalMapEffect )
    {
      // NormalMapEffect
      if ( mSharing && info.name && *info.name )
      {
        auto it = mEffectNormalMap.find( info.name );
        if ( mSharing && it != mEffectNormalMap.end() )
        {
          return it->second;
        }
      }

      auto effect = std::make_shared<NormalMapEffect>( device.Get() );

      effect->EnableDefaultLighting();

      effect->SetAlpha( info.alpha );

      if ( info.perVertexColor )
      {
        effect->SetVertexColorEnabled( true );
      }

      // NormalMap Effect does not have an ambient material color

      XMVECTOR color = XMLoadFloat3( &info.diffuseColor );
      effect->SetDiffuseColor( color );

      if ( info.specularColor.x != 0 || info.specularColor.y != 0 || info.specularColor.z != 0 )
      {
        color = XMLoadFloat3( &info.specularColor );
        effect->SetSpecularColor( color );
        effect->SetSpecularPower( info.specularPower );
      }
      else
      {
        effect->DisableSpecular();
      }

      if ( info.emissiveColor.x != 0 || info.emissiveColor.y != 0 || info.emissiveColor.z != 0 )
      {
        color = XMLoadFloat3( &info.emissiveColor );
        effect->SetEmissiveColor( color );
      }

      if ( info.diffuseTexture && *info.diffuseTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.diffuseTexture, deviceContext, &srv );

        effect->SetTexture( srv.Get() );
      }

      if ( info.specularTexture && *info.specularTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.specularTexture, deviceContext, &srv );

        effect->SetSpecularTexture( srv.Get() );
      }

      if ( info.normalTexture && *info.normalTexture )
      {
        ComPtr<ID3D11ShaderResourceView> srv;

        factory->CreateTexture( info.normalTexture, deviceContext, &srv );

        effect->SetNormalTexture( srv.Get() );
      }

      if ( mSharing && info.name && *info.name )
      {
        std::lock_guard<std::mutex> lock( mutex );
        mEffectNormalMap.insert( EffectCache::value_type( info.name, effect ) );
      }

      return effect;
    }
    else
    {
      */
    // BasicEffect
    if ( mSharing && info.name && *info.name )
    {
      auto it = mEffectCache.find( info.name );
      if ( mSharing && it != mEffectCache.end() )
      {
        return it->second;
      }
    }

    auto effect = std::make_shared<TrackedUltrasound::Rendering::InstancedBasicEffect>( device.Get() );

    effect->EnableDefaultLighting();
    effect->SetLightingEnabled( true );

    effect->SetAlpha( info.alpha );

    if ( info.perVertexColor )
    {
      effect->SetVertexColorEnabled( true );
    }

    // Basic Effect does not have an ambient material color

    XMVECTOR color = XMLoadFloat3( &info.diffuseColor );
    effect->SetDiffuseColor( color );

    if ( info.specularColor.x != 0 || info.specularColor.y != 0 || info.specularColor.z != 0 )
    {
      color = XMLoadFloat3( &info.specularColor );
      effect->SetSpecularColor( color );
      effect->SetSpecularPower( info.specularPower );
    }
    else
    {
      effect->DisableSpecular();
    }

    if ( info.emissiveColor.x != 0 || info.emissiveColor.y != 0 || info.emissiveColor.z != 0 )
    {
      color = XMLoadFloat3( &info.emissiveColor );
      effect->SetEmissiveColor( color );
    }

    if ( info.diffuseTexture && *info.diffuseTexture )
    {
      ComPtr<ID3D11ShaderResourceView> srv;

      factory->CreateTexture( info.diffuseTexture, deviceContext, &srv );

      effect->SetTexture( srv.Get() );
      effect->SetTextureEnabled( true );
    }

    if ( mSharing && info.name && *info.name )
    {
      std::lock_guard<std::mutex> lock( mutex );
      mEffectCache.insert( EffectCache::value_type( info.name, effect ) );
    }

    return effect;

    //}
  }

  _Use_decl_annotations_
  void InstancedEffectFactory::Impl::CreateTexture( const wchar_t* name, ID3D11DeviceContext* deviceContext, ID3D11ShaderResourceView** textureView )
  {
    if ( !name || !textureView )
    {
      throw std::exception( "invalid arguments" );
    }

    auto it = mTextureCache.find( name );

    if ( mSharing && it != mTextureCache.end() )
    {
      ID3D11ShaderResourceView* srv = it->second.Get();
      srv->AddRef();
      *textureView = srv;
    }
    else
    {
      wchar_t fullName[MAX_PATH] = {};
      wcscpy_s( fullName, mPath );
      wcscat_s( fullName, name );

      WIN32_FILE_ATTRIBUTE_DATA fileAttr = {};
      if ( !GetFileAttributesExW( fullName, GetFileExInfoStandard, &fileAttr ) )
      {
        // Try Current Working Directory (CWD)
        wcscpy_s( fullName, name );
        if ( !GetFileAttributesExW( fullName, GetFileExInfoStandard, &fileAttr ) )
        {
          DebugTrace( "EffectFactory could not find texture file '%ls'\n", name );
          throw std::exception( "CreateTexture" );
        }
      }

      wchar_t ext[_MAX_EXT];
      _wsplitpath_s( name, nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT );

      if ( _wcsicmp( ext, L".dds" ) == 0 )
      {
        HRESULT hr = CreateDDSTextureFromFile( device.Get(), fullName, nullptr, textureView );
        if ( FAILED( hr ) )
        {
          DebugTrace( "CreateDDSTextureFromFile failed (%08X) for '%ls'\n", hr, fullName );
          throw std::exception( "CreateDDSTextureFromFile" );
        }
      }
      else if ( deviceContext )
      {
        std::lock_guard<std::mutex> lock( mutex );
        HRESULT hr = CreateWICTextureFromFile( device.Get(), deviceContext, fullName, nullptr, textureView );
        if ( FAILED( hr ) )
        {
          DebugTrace( "CreateWICTextureFromFile failed (%08X) for '%ls'\n", hr, fullName );
          throw std::exception( "CreateWICTextureFromFile" );
        }
      }
      else
      {
        HRESULT hr = CreateWICTextureFromFile( device.Get(), fullName, nullptr, textureView );
        if ( FAILED( hr ) )
        {
          DebugTrace( "CreateWICTextureFromFile failed (%08X) for '%ls'\n", hr, fullName );
          throw std::exception( "CreateWICTextureFromFile" );
        }
      }

      if ( mSharing && *name && it == mTextureCache.end() )
      {
        std::lock_guard<std::mutex> lock( mutex );
        mTextureCache.insert( TextureCache::value_type( name, *textureView ) );
      }
    }
  }

  void InstancedEffectFactory::Impl::ReleaseCache()
  {
    std::lock_guard<std::mutex> lock( mutex );
    mEffectCache.clear();
    mEffectCacheSkinning.clear();
    mEffectCacheDualTexture.clear();
    mEffectNormalMap.clear();
    mTextureCache.clear();
  }

  //--------------------------------------------------------------------------------------
  InstancedEffectFactory::InstancedEffectFactory( _In_ ID3D11Device* device )
    : pImpl( Impl::instancePool.DemandCreate( device ) )
  {
  }

  InstancedEffectFactory::~InstancedEffectFactory()
  {
  }

  InstancedEffectFactory::InstancedEffectFactory( InstancedEffectFactory&& moveFrom )
    : pImpl( std::move( moveFrom.pImpl ) )
  {
  }

  InstancedEffectFactory& InstancedEffectFactory::operator= ( InstancedEffectFactory&& moveFrom )
  {
    pImpl = std::move( moveFrom.pImpl );
    return *this;
  }

  _Use_decl_annotations_
  std::shared_ptr<IEffect> InstancedEffectFactory::CreateEffect( const EffectInfo& info, ID3D11DeviceContext* deviceContext )
  {
    return pImpl->CreateEffect( this, info, deviceContext );
  }

  _Use_decl_annotations_
  void InstancedEffectFactory::CreateTexture( const wchar_t* name, ID3D11DeviceContext* deviceContext, ID3D11ShaderResourceView** textureView )
  {
    return pImpl->CreateTexture( name, deviceContext, textureView );
  }

  void InstancedEffectFactory::ReleaseCache()
  {
    pImpl->ReleaseCache();
  }

  void InstancedEffectFactory::SetSharing( bool enabled )
  {
    pImpl->SetSharing( enabled );
  }

  void InstancedEffectFactory::SetUseNormalMapEffect( bool enabled )
  {
    pImpl->SetUseNormalMapEffect( enabled );
  }

  void InstancedEffectFactory::SetDirectory( _In_opt_z_ const wchar_t* path )
  {
    if ( path && *path != 0 )
    {
      wcscpy_s( pImpl->mPath, path );
      size_t len = wcsnlen( pImpl->mPath, MAX_PATH );
      if ( len > 0 && len < ( MAX_PATH - 1 ) )
      {
        // Ensure it has a trailing slash
        if ( pImpl->mPath[len - 1] != L'\\' )
        {
          pImpl->mPath[len] = L'\\';
          pImpl->mPath[len + 1] = 0;
        }
      }
    }
    else
    {
      *pImpl->mPath = 0;
    }
  }
}