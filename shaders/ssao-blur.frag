#version 330 core

in vec2 vUV;
out float FragOcclusion;

uniform sampler2D uOcclusion;
uniform sampler2D uPositions;
uniform sampler2D uNormals;

void main() {
    vec4 center = texture(uPositions, vUV);
    vec3 normal = texture(uNormals, vUV).xyz;
    if (center.a < 0.5) {
        FragOcclusion = 1.0;
        return;
    }
    vec2 texel = 1.0 / vec2(textureSize(uOcclusion, 0));
    float total = 0.0;
    float weight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + texel * vec2(x, y);
            vec4 p = texture(uPositions, uv);
            if (p.a < 0.5) continue;
            vec3 n = texture(uNormals, uv).xyz;
            float spatial = exp(-float(x*x + y*y) / 6.0);
            float depthWeight = exp(-abs(center.z - p.z) * 8.0);
            float normalWeight = pow(max(dot(normal, n), 0.0), 8.0);
            float w = spatial * depthWeight * normalWeight;
            total += texture(uOcclusion, uv).r * w;
            weight += w;
        }
    }
    FragOcclusion = total / max(weight, 0.0001);
}
