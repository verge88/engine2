#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uPanorama;
uniform samplerCube uEnvironment;
uniform int uFace;
uniform int uMode; // 0=equirect->cube, 1=diffuse irradiance, 2=GGX prefilter
uniform float uRoughness;

const float PI = 3.14159265358979323846;

vec3 faceDirection(vec2 uv, int face) {
    vec2 p = uv * 2.0 - 1.0;
    if (face == 0) return normalize(vec3( 1.0, -p.y, -p.x));
    if (face == 1) return normalize(vec3(-1.0, -p.y,  p.x));
    if (face == 2) return normalize(vec3( p.x,  1.0,  p.y));
    if (face == 3) return normalize(vec3( p.x, -1.0, -p.y));
    if (face == 4) return normalize(vec3( p.x, -p.y,  1.0));
    return normalize(vec3(-p.x, -p.y, -1.0));
}

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint index, uint count) {
    return vec2(float(index) / float(count), radicalInverse(index));
}

mat3 tangentFrame(vec3 N) {
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    return mat3(tangent, cross(N, tangent), N);
}

vec3 importanceSampleGGX(vec2 xi, vec3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / max(1.0 + (a * a - 1.0) * xi.y, 0.00001));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return normalize(tangentFrame(N) * H);
}

void main() {
    vec3 N = faceDirection(vUV, uFace);

    if (uMode == 0) {
        vec2 uv = vec2(atan(N.z, N.x) / (2.0 * PI) + 0.5,
                       asin(clamp(N.y, -1.0, 1.0)) / PI + 0.5);
        FragColor = vec4(texture(uPanorama, uv).rgb, 1.0);
        return;
    }

    vec3 result = vec3(0.0);
    const uint SAMPLE_COUNT = 64u;
    if (uMode == 1) {
        // Cosine-weighted hemisphere: E = PI * average(radiance).
        mat3 frame = tangentFrame(N);
        for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
            vec2 xi = hammersley(i, SAMPLE_COUNT);
            float phi = 2.0 * PI * xi.x;
            float r = sqrt(xi.y);
            vec3 hemisphere = vec3(cos(phi) * r, sin(phi) * r,
                                   sqrt(max(1.0 - xi.y, 0.0)));
            result += textureLod(uEnvironment, frame * hemisphere, 0.0).rgb;
        }
        result *= PI / float(SAMPLE_COUNT);
    } else {
        // GGX-prefiltered radiance for the split-sum specular IBL approximation.
        vec3 V = N;
        float weight = 0.0;
        for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
            vec3 H = importanceSampleGGX(hammersley(i, SAMPLE_COUNT), N,
                                          max(uRoughness, 0.001));
            vec3 L = normalize(2.0 * dot(V, H) * H - V);
            float ndotl = max(dot(N, L), 0.0);
            if (ndotl > 0.0) {
                result += textureLod(uEnvironment, L, 0.0).rgb * ndotl;
                weight += ndotl;
            }
        }
        result /= max(weight, 0.0001);
    }
    FragColor = vec4(result, 1.0);
}
