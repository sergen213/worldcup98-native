#version 450
#extension GL_GOOGLE_include_directive : require
// 2D UI quads in the virtual 640x480 space.
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outEyeDist;

void main() {
    gl_Position = frame.uiProj * vec4(inPos.xy, 0.0, 1.0);
    outEyeDist = -1.0; // never fogged
    outUv = inUv;
    outColor = inColor;
}
