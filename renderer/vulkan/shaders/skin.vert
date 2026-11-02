#version 450
#extension GL_GOOGLE_include_directive : require
// PCMD skinned meshes: every influence carries its own position in its joint's frame
// (docs/formats/PCMD.md), position = sum_i w_i * (J[joint_i] * p_i).
#include "common.glsl"
layout(std430, set = 0, binding = 1) readonly buffer Joints { mat4 joints[]; };

layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec3 inP0;
layout(location = 5) in vec3 inP1;
layout(location = 6) in vec3 inP2;
layout(location = 7) in vec3 inWeight;
layout(location = 8) in uvec4 inJoints;
layout(location = 9) in vec4 inLight0;
layout(location = 10) in vec4 inLight1;
layout(location = 11) in vec4 inLight2;
layout(location = 12) in vec4 inLight3;

layout(location = 0) out vec2 outUv;
layout(location = 1) out vec4 outColor;
layout(location = 2) out float outEyeDist;

void main() {
    uint base = pc.iparams.x;
    vec3 p = inWeight.x * (joints[base + inJoints.x] * vec4(inP0, 1.0)).xyz
           + inWeight.y * (joints[base + inJoints.y] * vec4(inP1, 1.0)).xyz
           + inWeight.z * (joints[base + inJoints.z] * vec4(inP2, 1.0)).xyz;
    vec4 world = pc.model * vec4(p, 1.0);
    gl_Position = frame.viewProj * world;
    outEyeDist = length((frame.view * world).xyz);
    outUv = inUv;
    vec4 l = pc.light;
    outColor = dot(l, vec4(1.0)) > 0.0
        ? vec4(min(l.x * inLight0.rgb + l.y * inLight1.rgb + l.z * inLight2.rgb + l.w * inLight3.rgb, vec3(1.0)), inColor.a)
        : inColor;
}
