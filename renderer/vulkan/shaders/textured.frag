#version 450
#extension GL_GOOGLE_include_directive : require
// Texture * vertex colour * colour scale, optional alpha test, linear eye-distance fog.
#include "common.glsl"
layout(set = 1, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 inUv;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inEyeDist;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = texture(tex, inUv) * inColor;
    if (c.a < pc.params.x) discard;
    c.rgb = min(c.rgb * pc.params.y, vec3(1.0));
    if (frame.fogColor.a > 0.0 && inEyeDist >= 0.0) {
        float f = clamp((frame.fogRange.y - inEyeDist) / (frame.fogRange.y - frame.fogRange.x), 0.0, 1.0);
        c.rgb = mix(frame.fogColor.rgb, c.rgb, f);
    }
    outColor = c;
}
