#version 330 core

in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uImage;
uniform vec2 uDirection;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
    vec3 result = texture(uImage, vUV).rgb * 0.227027;
    result += texture(uImage, vUV + uDirection * texel * 1.384615).rgb * 0.316216;
    result += texture(uImage, vUV - uDirection * texel * 1.384615).rgb * 0.316216;
    result += texture(uImage, vUV + uDirection * texel * 3.230769).rgb * 0.070270;
    result += texture(uImage, vUV - uDirection * texel * 3.230769).rgb * 0.070270;
    FragColor = vec4(result, 1.0);
}
