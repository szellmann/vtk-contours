#include <iostream>
#include <stdio.h>
#include "vecmath.h"
#define uniform
#define in
typedef unsigned int uint;
typedef vecmath::vec2f vec2;
typedef vecmath::vec3f vec3;
typedef vecmath::vec4f vec4;
typedef vecmath::vec2i ivec2;
typedef vecmath::vec3i ivec3;
typedef vecmath::vec4i ivec4;
typedef vecmath::vec2ui uvec2;
typedef vecmath::vec3ui uvec3;
typedef vecmath::vec4ui uvec4;

#define sampler2D vec4**
#define usampler2D uvec4**

#define SHADER_MAIN extern "C" int main

#define DBG 1

inline
vec4 texelFetch(sampler2D sampler, ivec2 tcoord, int /*mipLevel*/) {
  return sampler[tcoord.y][tcoord.x];
}

inline
uvec4 texelFetch(usampler2D sampler, ivec2 tcoord, int /*mipLevel*/) {
  return sampler[tcoord.y][tcoord.x];
}

inline
vec4 texture(sampler2D sampler, const vec2 tcoord) {
  return {};
}

vec4 gl_FragData[512];

#include "segmentation.glsl"

void printFragData(int i) {
  printf("gl_FragData[%i]: %f,%f,%f,%f\n", i,
         gl_FragData[i].x,
         gl_FragData[i].y,
         gl_FragData[i].z,
         gl_FragData[i].w);
}

extern "C" void exec(float u, float v) {
  tcoordVCVSOutput = vec2(u,v);
  main();
  printFragData(0);
}

