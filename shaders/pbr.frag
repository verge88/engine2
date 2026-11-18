#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;
in vec4 vLightPosition;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAlbedo;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uOcclusionStrength;
uniform float uNormalScale;
uniform int uChecker;
uniform int uDoubleSided;

uniform sampler2D uShadowMap;
uniform sampler2D uBaseColorTexture;
uniform sampler2D uMetallicRoughnessTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uOcclusionTexture;
uniform sampler2D uEmissiveTexture;
uniform int uHasBaseColorTexture;
uniform int uHasMetallicRoughnessTexture;
uniform int uHasNormalTexture;
uniform int uHasOcclusionTexture;
uniform int uHasEmissiveTexture;

uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D uBrdfLut;
uniform sampler2D uSSAOTex;
uniform int uUseSSAO;
uniform vec2 uViewport;

const int MAX_POINT_LIGHTS = 8;
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float range;
};
uniform PointLight uPointLights[MAX_POINT_LIGHTS];
uniform int uPointLightCount;

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


// Direct Cook-Torrance BRDF evaluated for the sun and for every point light.
vec3 evaluateLight(vec3 N, vec3 V, vec3 L, vec3 radiance,
                   vec3 albedo, vec3 f0, float metallic, float roughness) {
    float ndotl = max(dot(N, L), 0.0);
    if (ndotl <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), f0);
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3 specular = (D * G * F) /
                    max(4.0 * max(dot(N, V), 0.0) * ndotl, 0.0001);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    return (kd * albedo / PI + specular) * radiance * ndotl;
}

void main() {
    vec3 baseColor = uAlbedo;
    if (uHasBaseColorTexture != 0) {
        // GL_SRGB8_ALPHA8 automatically converts the sample to linear space.
        baseColor *= texture(uBaseColorTexture, vUV).rgb;
    }
    if (uChecker != 0) {
        vec2 cell = floor(vWorldPosition.xz * 0.5);
        float pattern = mod(cell.x + cell.y, 2.0);
        baseColor = mix(vec3(0.19, 0.205, 0.22), vec3(0.30, 0.31, 0.33), pattern);
    }

    float metallic = clamp(uMetallic, 0.0, 1.0);
    float roughness = clamp(uRoughness, 0.045, 1.0);
    if (uHasMetallicRoughnessTexture != 0) {
        vec3 mr = texture(uMetallicRoughnessTexture, vUV).rgb;
        metallic *= mr.b;
        roughness = clamp(roughness * mr.g, 0.045, 1.0);
    }

    vec3 N = normalize(vNormal);
    if (uDoubleSided != 0 && !gl_FrontFacing) N = -N;
    if (uHasNormalTexture != 0) {
        vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, N) * N);
        vec3 B = cross(N, T) * vTangent.w;
        vec3 mapNormal = texture(uNormalTexture, vUV).xyz * 2.0 - 1.0;
        mapNormal.xy *= uNormalScale;
        N = normalize(mat3(T, B, N) * normalize(mapNormal));
    }

    vec3 V = normalize(uViewPos - vWorldPosition);
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    vec3 L = normalize(uLightDir);
    vec3 direct = evaluateLight(N, V, L,
        uLightColor * (1.0 - shadowFactor(N)), baseColor, f0, metallic, roughness);

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (i >= uPointLightCount) break;
        vec3 toLight = uPointLights[i].position - vWorldPosition;
        float distanceToLight = length(toLight);
        float range = max(uPointLights[i].range, 0.001);
        float normalizedDistance = clamp(distanceToLight / range, 0.0, 1.0);
        float fade = pow(max(1.0 - pow(normalizedDistance, 4.0), 0.0), 2.0);
        float attenuation = fade / max(distanceToLight * distanceToLight, 0.25);
        vec3 radiance = uPointLights[i].color *
                         (uPointLights[i].intensity * attenuation);
        direct += evaluateLight(N, V, normalize(toLight), radiance,
                                baseColor, f0, metallic, roughness);
    }

    // Physically based split-sum IBL: diffuse irradiance / PI plus GGX
    // prefiltered specular radiance integrated with the BRDF lookup table.
    float ndotv = max(dot(N, V), 0.0);
    vec3 fresnelIBL = f0 + (max(vec3(1.0 - roughness), f0) - f0) *
                         pow(1.0 - ndotv, 5.0);
    vec3 kdIBL = (vec3(1.0) - fresnelIBL) * (1.0 - metallic);
    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * baseColor / PI;

    vec3 reflection = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilterMap, reflection, roughness * 4.0).rgb;
    vec2 brdf = texture(uBrdfLut, vec2(ndotv, roughness)).rg;
    vec3 specularIBL = prefiltered * (fresnelIBL * brdf.x + brdf.y);
    vec3 ambient = kdIBL * diffuseIBL + specularIBL;
    if (uHasOcclusionTexture != 0) {
        float ao = texture(uOcclusionTexture, vUV).r;
        ambient *= mix(1.0, ao, uOcclusionStrength);
    }
    if (uUseSSAO != 0) {
        // SSAO is an approximation of local ambient visibility.
        // Never darken direct light, emissions or the sky with SSAO.
        ambient *= texture(uSSAOTex, gl_FragCoord.xy / uViewport).r;
    }

    vec3 emissive = uEmissive;
    if (uHasEmissiveTexture != 0) {
        emissive *= texture(uEmissiveTexture, vUV).rgb;
    }

    // Stay linear and HDR until the postprocess composite pass.
    FragColor = vec4(direct + ambient + emissive, 1.0);
}
