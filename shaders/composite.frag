#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uExposure;
uniform float uBloomStrength;

vec3 acesApprox(vec3 color) {
    return clamp((color * (2.51 * color + 0.03)) /
                 (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uBloomStrength > 0.0)
        hdr += texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 mapped = acesApprox(hdr * max(uExposure, 0.001));
    // Default framebuffer is treated as linear; explicit gamma encodes sRGB.
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
