#version 330 core

in vec3 vViewPosition;
in vec3 vViewNormal;
in vec2 vUV;

layout(location = 0) out vec4 FragPosition;
layout(location = 1) out vec4 FragNormal;

uniform int uDoubleSided;
uniform int uAlphaMode;
uniform float uAlphaFactor;
uniform float uAlphaCutoff;
uniform int uHasBaseColorTexture;
uniform sampler2D uBaseColorTexture;

void main() {
    if (uAlphaMode == 1) {
        float alpha = uAlphaFactor;
        if (uHasBaseColorTexture != 0)
            alpha *= texture(uBaseColorTexture, vUV).a;
        if (alpha < uAlphaCutoff) discard;
    }
    vec3 normal = normalize(vViewNormal);
    if (uDoubleSided != 0 && !gl_FrontFacing) normal = -normal;
    FragPosition = vec4(vViewPosition, 1.0);
    FragNormal = vec4(normal, 1.0);
}
