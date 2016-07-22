//--------------------------------------------------------------------------------------
// File: CSRayTriangleIntersection.hlsl
//
// This file contains the Compute Shader to calculate the intersection of a ray and a triangle
// 
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

// TODO : add a constant buffer with the ray details

struct InputBufferType
{
  double vertexOne[3];
  double vertexTwo[3];
  double vertexThree[3];
};

struct OutputBufferType
{
  double intersectionPoint[3];
};

StructuredBuffer<InputBufferType> Buffer0 : register(t0);
StructuredBuffer<InputBufferType> Buffer1 : register(t1);
RWStructuredBuffer<OutputBufferType> BufferOut : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  //BufferOut[DTid.x].i = Buffer0[DTid.x].i + Buffer1[DTid.x].i;
  //BufferOut[DTid.x].f = Buffer0[DTid.x].f + Buffer1[DTid.x].f;
  //BufferOut[DTid.x].d = Buffer0[DTid.x].d + Buffer1[DTid.x].d;
}