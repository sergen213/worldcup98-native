#version 450
#extension GL_GOOGLE_include_directive : require
// Rigid meshes: world transform from push constants, camera from the per-frame UBO.
#include "common.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outEyeDist;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    gl_Position = frame.viewProj * world;
    outEyeDist = length((frame.view * world).xyz);
    outUv = inUv;
    outColor = inColor;
}
