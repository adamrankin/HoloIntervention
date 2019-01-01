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

namespace Valhalla
{
  template<typename T> bool GetScalarAttribute(const std::wstring& attributeName, Windows::Data::Xml::Dom::IXmlNode^ node, T& outValue)
  {
    if (!HasAttribute(attributeName, node))
    {
      return false;
    }

    auto attribute = node->Attributes->GetNamedItem(ref new Platform::String(attributeName.c_str()));
    if (attribute != nullptr)
    {
      Platform::String^ attributeContent = dynamic_cast<Platform::String^>(attribute->NodeValue);
      if (!attributeContent->IsEmpty())
      {
        auto str = std::wstring(attributeContent->Data());
        std::wstringstream ss;
        ss << str;
        try
        {
          ss >> outValue;
          return true;
        }
        catch (const std::exception&) {}
      }

      return false;
    }

    return false;
  }
}