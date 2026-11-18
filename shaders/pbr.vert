#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;
out float vViewDepth;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vWorldPosition = worldPosition.xyz;
    vNormal = normalize(normalMatrix * aNormal);
    vTangent = vec4(normalize(mat3(uModel) * aTangent.xyz),
                    aTangent.w * sign(determinant(mat3(uModel))));
    vUV = aUV;
    vec4 viewPosition = uView * worldPosition;
    vViewDepth = -viewPosition.z;
    gl_Position = uProjection * viewPosition;
}
