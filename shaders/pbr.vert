#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpace;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec4 vLightPosition;

void main() {
    vec4 worldPosition = uModel * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;
    vNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    vLightPosition = uLightSpace * worldPosition;
    gl_Position = uProjection * uView * worldPosition;
}
