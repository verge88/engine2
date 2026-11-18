#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform float uScatter;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    // 3x3 tent kernel; added into the next-larger bloom mip with additive
    // blending. Each pass reads a *different* texture to avoid feedback.
    vec3 result = texture(uSource, vUV).rgb * 4.0;
    result += texture(uSource, vUV + texel * vec2(-1, 0)).rgb * 2.0;
    result += texture(uSource, vUV + texel * vec2( 1, 0)).rgb * 2.0;
    result += texture(uSource, vUV + texel * vec2( 0,-1)).rgb * 2.0;
    result += texture(uSource, vUV + texel * vec2( 0, 1)).rgb * 2.0;
    result += texture(uSource, vUV + texel * vec2(-1,-1)).rgb;
    result += texture(uSource, vUV + texel * vec2(-1, 1)).rgb;
    result += texture(uSource, vUV + texel * vec2( 1,-1)).rgb;
    result += texture(uSource, vUV + texel * vec2( 1, 1)).rgb;
    FragColor = vec4(max(result / 16.0, vec3(0.0)) * uScatter, 1.0);
}
