// Shared declarations (included by every shader).
layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 viewProj;
    mat4 uiProj;
    mat4 view;
    vec4 fogColor; // rgb, a = 1 when fog is enabled
    vec4 fogRange; // x = start, y = end (eye distance)
} frame;
// params: x = alpha cutoff, y = colour scale; iparams.x = first joint matrix (skinned meshes)
layout(push_constant) uniform Push { mat4 model; vec4 params; uvec4 iparams; vec4 light; } pc;
