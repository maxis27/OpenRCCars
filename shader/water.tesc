#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.glsl"

// Splits the sea finely near the camera so that water.tese can move it with the waves.
layout(vertices = 3) out;

layout(location = 0) in vec3 inNormal[];
layout(location = 1) in vec2 inUvBase[];
layout(location = 2) in vec2 inUvLight[];
layout(location = 3) in vec2 inUvMask[];
layout(location = 4) in vec2 inUvBase2[];
layout(location = 5) in vec3 inWorld[];

layout(location = 0) out vec3 outNormal[];
layout(location = 1) out vec2 outUvBase[];
layout(location = 2) out vec2 outUvLight[];
layout(location = 3) out vec2 outUvMask[];
layout(location = 4) out vec2 outUvBase2[];
layout(location = 5) out vec3 outWorld[];

// how many pieces an edge is cut into: depends only on the edge, so that both triangles sharing it agree
float edgeLevel(vec3 a, vec3 b) {
  float spacing = waterSpacing(distance(push.cameraPos.xyz, (a+b)*0.5));
  return max(distance(a, b)/spacing, 1.0);
  }

void main() {
  outNormal [gl_InvocationID] = inNormal [gl_InvocationID];
  outUvBase [gl_InvocationID] = inUvBase [gl_InvocationID];
  outUvLight[gl_InvocationID] = inUvLight[gl_InvocationID];
  outUvMask [gl_InvocationID] = inUvMask [gl_InvocationID];
  outUvBase2[gl_InvocationID] = inUvBase2[gl_InvocationID];
  outWorld  [gl_InvocationID] = inWorld  [gl_InvocationID];

  if(gl_InvocationID==0) {
    // outer level i is the edge opposite corner i
    float e0 = edgeLevel(inWorld[1], inWorld[2]);
    float e1 = edgeLevel(inWorld[2], inWorld[0]);
    float e2 = edgeLevel(inWorld[0], inWorld[1]);
    float e  = max(e0, max(e1, e2));
    gl_TessLevelOuter[0] = min(e0, 64.0);
    gl_TessLevelOuter[1] = min(e1, 64.0);
    gl_TessLevelOuter[2] = min(e2, 64.0);
    gl_TessLevelInner[0] = min(e,  64.0);
    }
  }
