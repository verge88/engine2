#version 330 core

in vec3 vWorldPosition;
in vec2 vUV;

uniform vec3 uLightPosition;
uniform float uFarPlane;
uniform int uAlphaMode;
uniform float uAlphaFactor;
uniform float uAlphaCutoff;
uniform int uHasBaseColorTexture;
uniform sampler2D uBaseColorTexture;

void main() {
    // Keep alpha-test silhouettes in all six faces, like the sun shadow pass.
    if (uAlphaMode == 1) {
        float alpha = uAlphaFactor;
        if (uHasBaseColorTexture != 0) {
            alpha *= texture(uBaseColorTexture, vUV).a;
        }
        if (alpha < uAlphaCutoff) discard;
    }
    // Linear radial depth, unlike OpenGL's non-linear projected depth.
    // This exactly matches the distance comparison in the lighting pass.
    gl_FragDepth = clamp(distance(vWorldPosition, uLightPosition) /
                         uFarPlane, 0.0, 1.0);
}
