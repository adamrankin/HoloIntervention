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

cbuffer WorldConstantBuffer : register(b0)
{
  float4x4 meshToWorld;
};

cbuffer RayConstantBuffer : register(b1)
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
  float4 intersectionEdge;
  bool intersection;
};

StructuredBuffer<VertexBufferType> meshBuffer : register(t0);
StructuredBuffer<IndexBufferType> indexBuffer : register(t1);
RWStructuredBuffer<OutputBufferType> resultBuffer : register(u0);

#define EPSILON 0.000001

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
  // Algorithm courtesy of https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
  // See:
  //    1. Möller T, Trumbore B. Fast, Minimum Storage Ray-triangle Intersection. J Graph Tools. 1997 Oct;2(1):21–28. 

  float4 v0 = {meshBuffer[indexBuffer[(DTid.x * 3)].index].vertex.x, meshBuffer[indexBuffer[(DTid.x * 3)].index].vertex.y, meshBuffer[indexBuffer[(DTid.x * 3)].index].vertex.z, 1};
  float4 v1 = {meshBuffer[indexBuffer[(DTid.x * 3) + 1].index].vertex.x, meshBuffer[indexBuffer[(DTid.x * 3) + 1].index].vertex.y, meshBuffer[indexBuffer[(DTid.x * 3) + 1].index].vertex.z, 1};
  float4 v2 = {meshBuffer[indexBuffer[(DTid.x * 3) + 2].index].vertex.x, meshBuffer[indexBuffer[(DTid.x * 3) + 2].index].vertex.y, meshBuffer[indexBuffer[(DTid.x * 3) + 2].index].vertex.z, 1};

  // Transform the vertex position into world space.
  v0 = mul(meshToWorld, v0);
  v1 = mul(meshToWorld, v1);
  v2 = mul(meshToWorld, v2);

  //Find vectors for two edges sharing v0
  float3 e1 = v1.xyz - v0.xyz;
  float3 e2 = v2.xyz - v0.xyz;

  //Begin calculating determinant - also used to calculate u parameter
  float3 P = cross(rayDirection.xyz, e2);

  //if determinant is near zero, ray lies in plane of triangle or ray is parallel to plane of triangle
  float det = dot(e1, P);

  //NOT CULLING
  if(det > -EPSILON && det < EPSILON)
  {
    return;
  }

  float inv_det = 1.f / det;

  //calculate distance from V1 to ray origin
  float3 T = rayOrigin.xyz - v0.xyz;

  //Calculate u parameter and test bound
  float u = dot(T, P) * inv_det;

  //The intersection lies outside of the triangle
  if(u < 0.f || u > 1.f)
  {
    return;
  }

  //Prepare to test v parameter
  float3 Q = cross(T, e1);

  //Calculate V parameter and test bound
  float v = dot(rayDirection.xyz, Q) * inv_det;

  //The intersection lies outside of the triangle
  if(v < 0.f || u + v > 1.f)
  {
    return;
  }

  float t = dot(e2, Q) * inv_det;

  if(t > EPSILON)
  {
    resultBuffer[0].intersection = true;

    // Calculate the normal of this triangle
    float3 normal = cross(e1, e2);

    // Normalize vectors to ensure valid coordinate system basis axis
    normal = normalize(normal);
    e1 = normalize(e1);

    //ray intersection
    resultBuffer[0].intersectionPoint.x = rayOrigin.x + t * rayDirection.x;
    resultBuffer[0].intersectionPoint.y = rayOrigin.y + t * rayDirection.y;
    resultBuffer[0].intersectionPoint.z = rayOrigin.z + t * rayDirection.z;
    resultBuffer[0].intersectionNormal = float4(normal, 1.f);
    resultBuffer[0].intersectionEdge = float4(e1, 1.f);
  }

  return;
}