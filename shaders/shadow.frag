#version 330 core

in vec2 vUV;

uniform int uAlphaMode;
uniform float uAlphaFactor;
uniform float uAlphaCutoff;
uniform int uHasBaseColorTexture;
uniform sampler2D uBaseColorTexture;

void main() {
    // Masked shadow silhouettes exactly follow the baseColor alpha channel.
    // BLEND objects are deliberately excluded from the depth pass on CPU.
    if (uAlphaMode == 1) {
        float alpha = uAlphaFactor;
        if (uHasBaseColorTexture != 0)
            alpha *= texture(uBaseColorTexture, vUV).a;
        if (alpha < uAlphaCutoff) discard;
    }
}
