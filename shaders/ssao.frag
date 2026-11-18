#version 330 core

in vec2 vUV;
out float FragOcclusion;

uniform sampler2D uPositions;
uniform sampler2D uNormals;
uniform mat4 uProjection;
uniform vec3 uSamples[16];

const int SAMPLE_COUNT = 16;
const float RADIUS = 0.65;
const float BIAS = 0.025;

void main() {
    vec4 position = texture(uPositions, vUV);
    vec4 normalSample = texture(uNormals, vUV);
    if (position.a < 0.5 || normalSample.a < 0.5) {
        FragOcclusion = 1.0;
        return;
    }
    vec3 N = normalize(normalSample.xyz);
    // Per-pixel deterministic rotation avoids repeating the same 16 directions.
    float angle = fract(sin(dot(floor(gl_FragCoord.xy),
                         vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.31);
    vec3 T = normalize(randomVec - N * dot(randomVec, N));
    vec3 B = cross(N, T);
    mat3 tbn = mat3(T, B, N);

    float occlusion = 0.0;
    float count = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        vec3 samplePosition = position.xyz + (tbn * uSamples[i]) * RADIUS;
        vec4 projected = uProjection * vec4(samplePosition, 1.0);
        if (projected.w <= 0.0) continue;
        vec2 sampleUV = projected.xy / projected.w * 0.5 + 0.5;
        if (any(lessThan(sampleUV, vec2(0.0))) ||
            any(greaterThan(sampleUV, vec2(1.0)))) continue;

        vec4 other = texture(uPositions, sampleUV);
        if (other.a < 0.5) continue;
        float difference = abs(position.z - other.z);
        float rangeWeight = smoothstep(0.0, 1.0, RADIUS / max(difference, 0.0001));
        occlusion += (other.z >= samplePosition.z + BIAS ? 1.0 : 0.0) * rangeWeight;
        count += 1.0;
    }
    FragOcclusion = clamp(1.0 - occlusion / max(count, 1.0), 0.0, 1.0);
}
