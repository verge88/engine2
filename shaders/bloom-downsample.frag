#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSource, 0));
    // 13-tap downsample: 4 corner quadrants and center, avoids the aliasing
    // that occurs when decimating very bright HDR pixels into small mips.
    vec3 a = texture(uSource, vUV + texel * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSource, vUV + texel * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(uSource, vUV + texel * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(uSource, vUV + texel * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(uSource, vUV).rgb;
    vec3 f = texture(uSource, vUV + texel * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(uSource, vUV + texel * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(uSource, vUV + texel * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(uSource, vUV + texel * vec2( 2.0,  2.0)).rgb;
    vec3 j = texture(uSource, vUV + texel * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(uSource, vUV + texel * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(uSource, vUV + texel * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(uSource, vUV + texel * vec2( 1.0,  1.0)).rgb;
    vec3 result = (a + c + g + i) * 0.03125 +
                  (b + d + f + h) * 0.06250 +
                  e * 0.12500 +
                  (j + k + l + m) * 0.12500;
    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
