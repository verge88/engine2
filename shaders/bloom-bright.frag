#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform float uThreshold;

void main() {
    vec3 hdr = max(texture(uScene, vUV).rgb, vec3(0.0));
    float luminance = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    // Continuous soft knee avoids hard edges and flickering in bloom.
    float knee = max(0.5 * uThreshold, 0.0001);
    float soft = clamp(luminance - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 0.0001);
    float contribution = max(luminance - uThreshold, soft);
    vec3 bloom = hdr * (contribution / max(luminance, 0.0001));
    FragColor = vec4(bloom, 1.0);
}
