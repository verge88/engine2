#version 330 core

in vec2 vUV;
out float FragLogLuminance;

uniform sampler2D uScene;

void main() {
    vec3 hdr = max(texture(uScene, vUV).rgb, vec3(0.0));
    float luminance = dot(hdr, vec3(0.2126, 0.7152, 0.0722));
    // Geometric rather than arithmetic mean preserves detail in scenes with
    // isolated hot pixels / bright highlights.
    FragLogLuminance = log(max(luminance, 0.0001));
}
