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
#include "TransformName.h"

// std includes
#include <sstream>

namespace HoloIntervention
{
  //-------------------------------------------------------
  TransformName::TransformName()
  {
  }

  //-------------------------------------------------------
  TransformName::~TransformName()
  {
  }

  //-------------------------------------------------------
  TransformName::TransformName( std::wstring aFrom, std::wstring aTo )
  {
    this->Capitalize( aFrom );
    this->m_From = aFrom;

    this->Capitalize( aTo );
    this->m_To = aTo;
  }

  //-------------------------------------------------------
  TransformName::TransformName( const std::wstring& transformName )
  {
    this->SetTransformName( transformName.c_str() );
  }

  //-------------------------------------------------------
  bool TransformName::IsValid() const
  {
    if ( this->m_From.empty() )
    {
      return false;
    }

    if ( this->m_To.empty() )
    {
      return false;
    }

    return true;
  }

  //-------------------------------------------------------
  bool TransformName::SetTransformName( const std::wstring& aTransformName )
  {
    this->m_From.clear();
    this->m_To.clear();

    size_t posTo = std::wstring::npos;

    // Check if the string has only one valid 'To' phrase
    int numOfMatch = 0;
    std::wstring subString = aTransformName;
    size_t posToTested = std::wstring::npos;
    size_t numberOfRemovedChars = 0;
    while ( ( ( posToTested = subString.find( L"To" ) ) != std::wstring::npos ) && ( subString.length() > posToTested + 2 ) )
    {
      if ( toupper( subString[posToTested + 2] ) == subString[posToTested + 2] )
      {
        // there is a "To", and after that the next letter is uppercase, so it's really a match (e.g., the first To in TestToolToTracker would not be a real match)
        numOfMatch++;
        posTo = numberOfRemovedChars + posToTested;
      }
      // search in the rest of the string
      subString = subString.substr( posToTested + 2 );
      numberOfRemovedChars += posToTested + 2;
    }

    if ( numOfMatch != 1 )
    {
      std::wstringstream wss;
      wss << L"Unable to parse transform name, there are " << numOfMatch << L" matching 'To' phrases in the transform name '" << aTransformName << L"', while exactly one allowed.";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }

    // Find <FrameFrom>To<FrameTo> matches
    if ( posTo == std::wstring::npos )
    {
      std::wstringstream wss;
      wss << L"Failed to set transform name - unable to find 'To' in '" << aTransformName << L"'!";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }
    else if ( posTo == 0 )
    {
      std::wstringstream wss;
      wss << L"Failed to set transform name - no coordinate frame name before 'To' in '" << aTransformName << L"'!";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }
    else if ( posTo == aTransformName.length() - 2 )
    {
      std::wstringstream wss;
      wss << L"Failed to set transform name - no coordinate frame name after 'To' in '" << aTransformName << L"'!";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }

    // Set From coordinate frame name
    this->m_From = aTransformName.substr( 0, posTo );

    // Allow handling of To coordinate frame containing "Transform"
    std::wstring postFrom( aTransformName.substr( posTo + 2 ) );
    if ( postFrom.find( L"Transform" ) != std::wstring::npos )
    {
      postFrom = postFrom.substr( 0, postFrom.find( L"Transform" ) );
    }

    this->m_To = postFrom;
    this->Capitalize( this->m_From );
    this->Capitalize( this->m_To );

    return true;
  }

  //-------------------------------------------------------
  bool TransformName::GetTransformName( std::wstring& aTransformName ) const
  {
    if ( this->m_From.empty() )
    {
      std::wstringstream wss;
      wss << L"Failed to get transform name - 'From' transform name is empty";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }

    if ( this->m_To.empty() )
    {
      std::wstringstream wss;
      wss << L"Failed to get transform name - 'To' transform name is empty";
      OutputDebugStringW( wss.str().c_str() );
      return false;
    }

    aTransformName = ( this->m_From + std::wstring( L"To" ) + this->m_To );
    return true;
  }

  //-------------------------------------------------------
  std::wstring TransformName::GetTransformName() const
  {
    return ( this->m_From + std::wstring( L"To" ) + this->m_To );
  }

  //-------------------------------------------------------
  std::wstring TransformName::From() const
  {
    return this->m_From;
  }

  //-------------------------------------------------------
  std::wstring TransformName::To() const
  {
    return this->m_To;
  }

  //-------------------------------------------------------
  void TransformName::Capitalize( std::wstring& aString )
  {
    // Change first character to uppercase
    if ( aString.length() < 1 )
    {
      return;
    }
    aString[0] = toupper( aString[0] );
  }

  //-------------------------------------------------------
  void TransformName::Clear()
  {
    this->m_From = L"";
    this->m_To = L"";
  }
}