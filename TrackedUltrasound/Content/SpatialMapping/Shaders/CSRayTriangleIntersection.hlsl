//--------------------------------------------------------------------------------------
// File: CSRayTriangleIntersection.hlsl
//
// This file contains the Compute Shader to calculate the intersection of a ray and a triangle
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------

cbuffer ConstantBuffer : register( b0 )
{
  float4 rayOrigin;
  float4 rayDirection;
};

struct VertexBufferType
{
  float4 vertex;
};

struct IndexBufferType
{
  uint index;
};

struct OutputBufferType
{
  float4 intersectionPoint;
  float4 intersectionNormal;
};

StructuredBuffer<VertexBufferType> meshBuffer : register( t0 );
StructuredBuffer<IndexBufferType> indexBuffer : register( t1 );
RWStructuredBuffer<OutputBufferType> resultBuffer : register( u0 );

#define EPSILON 0.000001

[numthreads( 1, 1, 1 )]
void main( uint3 DTid : SV_DispatchThreadID )
{
  // Algorithm courtesy of https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm

  float3 v0 = { meshBuffer[indexBuffer[( DTid.x * 3 )].index].vertex.x, meshBuffer[indexBuffer[( DTid.x * 3 )].index].vertex.y, meshBuffer[indexBuffer[( DTid.x * 3 )].index].vertex.z };
  float3 v1 = { meshBuffer[indexBuffer[( DTid.x * 3 ) + 1].index].vertex.x, meshBuffer[indexBuffer[( DTid.x * 3 ) + 1].index].vertex.y, meshBuffer[indexBuffer[( DTid.x * 3 ) + 1].index].vertex.z };
  float3 v2 = { meshBuffer[indexBuffer[( DTid.x * 3 ) + 2].index].vertex.x, meshBuffer[indexBuffer[( DTid.x * 3 ) + 2].index].vertex.y, meshBuffer[indexBuffer[( DTid.x * 3 ) + 2].index].vertex.z };

  float3 rayOrigin3 = { rayOrigin[0], rayOrigin[1], rayOrigin[2] };
  float3 rayDirection3 = { rayDirection[0], rayDirection[1], rayDirection[2] };

  //Find vectors for two edges sharing v0
  float3 e1 = v1 - v0;
  float3 e2 = v2 - v0;

  //Begin calculating determinant - also used to calculate u parameter
  float3 P = cross( rayDirection3, e2 );

  //if determinant is near zero, ray lies in plane of triangle or ray is parallel to plane of triangle
  float det = dot( e1, P );

  //NOT CULLING
  if ( det > -EPSILON && det < EPSILON )
  {
    return;
  }

  float inv_det = 1.f / det;

  //calculate distance from V1 to ray origin
  float3 T = rayOrigin3 - v0;

  //Calculate u parameter and test bound
  float u = dot( T, P ) * inv_det;

  //The intersection lies outside of the triangle
  if ( u < 0.f || u > 1.f )
  {
    return;
  }

  //Prepare to test v parameter
  float3 Q = cross( T, e1 );

  //Calculate V parameter and test bound
  float v = dot( rayDirection3, Q ) * inv_det;

  //The intersection lies outside of the triangle
  if ( v < 0.f || u + v  > 1.f )
  {
    return;
  }

  float t = dot( e2, Q ) * inv_det;

  if ( t > EPSILON )
  {
    // Calculate the normal of this triangle
    float3 normal = cross(e1, e2);

    //ray intersection
    resultBuffer[0].intersectionPoint.x = 1.2345;
    resultBuffer[0].intersectionPoint.y = t;
    resultBuffer[0].intersectionPoint.z = t;
    resultBuffer[0].intersectionNormal.x = normal.x;
    resultBuffer[0].intersectionNormal.y = normal.y;
    resultBuffer[0].intersectionNormal.z = normal.z;
  }

  // No hit, no win
  return;
}