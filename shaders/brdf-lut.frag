#version 330 core

in vec2 vUV;
layout(location = 0) out vec2 FragColor;

const float PI = 3.14159265359;

float radicalInverse(uint bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec3 importanceSampleGGX(vec2 xi, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / max(1.0 + (a*a - 1.0) * xi.y, 0.00001));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float a = roughness;
    float k = (a * a) * 0.5;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.00001);
}

void main() {
    float ndotv = clamp(vUV.x, 0.001, 1.0);
    float roughness = clamp(vUV.y, 0.045, 1.0);
    vec3 V = vec3(sqrt(1.0 - ndotv * ndotv), 0.0, ndotv);
    vec3 N = vec3(0.0, 0.0, 1.0);
    vec2 integrated = vec2(0.0);
    const uint SAMPLE_COUNT = 128u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i) {
        vec2 xi = vec2(float(i) / float(SAMPLE_COUNT), radicalInverse(i));
        vec3 H = importanceSampleGGX(xi, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float ndotl = max(L.z, 0.0);
        float ndoth = max(H.z, 0.0);
        float vdoth = max(dot(V, H), 0.0);
        if (ndotl > 0.0) {
            float G = geometrySchlickGGX(ndotl, roughness)
                    * geometrySchlickGGX(ndotv, roughness);
            float GVis = (G * vdoth) / max(ndoth * ndotv, 0.00001);
            float Fc = pow(1.0 - vdoth, 5.0);
            integrated += vec2((1.0 - Fc) * GVis, Fc * GVis);
        }
    }
    FragColor = integrated / float(SAMPLE_COUNT);
}
