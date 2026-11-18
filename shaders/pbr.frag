#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec4 vLightPosition;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform int uChecker;
uniform sampler2D uShadowMap;

const float PI = 3.14159265359;

float distributionGGX(vec3 normal, vec3 halfway, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = max(dot(normal, halfway), 0.0);
    float denom = ndoth * ndoth * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denom * denom, 0.000001);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.00001);
}

float geometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    float ndotv = max(dot(normal, viewDir), 0.0);
    float ndotl = max(dot(normal, lightDir), 0.0);
    return geometrySchlickGGX(ndotv, roughness) *
           geometrySchlickGGX(ndotl, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}

float shadowFactor(vec3 normal) {
    vec3 projected = vLightPosition.xyz / vLightPosition.w;
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.z < 0.0) return 0.0;
    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0) return 0.0;

    float bias = max(0.003 * (1.0 - max(dot(normal, uLightDir), 0.0)), 0.0005);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, projected.xy + vec2(x, y) * texel).r;
            shadow += (projected.z - bias > depth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

vec3 acesApprox(vec3 color) {
    return clamp((color * (2.51 * color + 0.03)) /
                 (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
    vec3 baseColor = uAlbedo;
    if (uChecker != 0) {
        vec2 cell = floor(vWorldPosition.xz * 0.5);
        float pattern = mod(cell.x + cell.y, 2.0);
        baseColor = mix(vec3(0.19, 0.205, 0.22), vec3(0.30, 0.31, 0.33), pattern);
    }

    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.045, 1.0);
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPosition);
    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);

    vec3 specular = (D * G * F) /
                    max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.0001);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    float ndotl = max(dot(N, L), 0.0);
    vec3 direct = (kd * baseColor / PI + specular) *
                  uLightColor * ndotl * (1.0 - shadowFactor(N));

    // Approximate fill lighting; replace with prefiltered environment maps for true IBL.
    float skyFacing = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(vec3(0.06, 0.053, 0.05), vec3(0.16, 0.20, 0.28), skyFacing);
    vec3 ambient = sky * baseColor * (1.0 - metallic) +
                   f0 * vec3(0.10, 0.11, 0.13) * (1.0 - roughness * 0.65);

    vec3 mapped = acesApprox(direct + ambient);
    FragColor = vec4(pow(mapped, vec3(1.0 / 2.2)), 1.0);
}
