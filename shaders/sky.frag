#version 330 core

in vec2 vUV;
out vec4 FragColor;

uniform samplerCube uEnvironment;
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;

void main() {
    vec4 point = uInverseViewProjection * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 direction = normalize(point.xyz / point.w - uCameraPosition);
    FragColor = vec4(textureLod(uEnvironment, direction, 0.0).rgb, 1.0);
}
