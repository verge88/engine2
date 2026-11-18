#version 330 core

in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;

void main() {
    vec3 color = texture(uScene, vUV).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    // Smooth threshold / knee around 1 nit in our relative HDR units.
    float soft = clamp(luminance - 0.5, 0.0, 1.0);
    soft = soft * soft * 0.5;
    float contribution = max(luminance - 1.0, soft);
    color *= contribution / max(luminance, 0.0001);
    FragColor = vec4(color, 1.0);
}
