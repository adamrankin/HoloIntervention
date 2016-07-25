#pragma once

namespace TrackedUltrasound
{
  namespace Spatial
  {
    struct VertexBufferType
    {
      float vertex[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct IndexBufferType
    {
      uint32 index;
    };

    struct OutputBufferType
    {
      float intersectionPoint[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      float intersectionNormal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct ConstantBuffer
    {
      // Constant buffers must have a a ByteWidth multiple of 16
      DirectX::XMFLOAT4X4 meshToWorld;
      float rayOrigin[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
      float rayDirection[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    static_assert((sizeof(ConstantBuffer) % (sizeof(float) * 4)) == 0, "Constant buffer size must be 16-byte aligned (16 bytes is the length of four floats).");
  }
}