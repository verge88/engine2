#version 330 core

in vec3 vViewPosition;
in vec3 vViewNormal;

layout(location = 0) out vec4 FragPosition;
layout(location = 1) out vec4 FragNormal;

uniform int uDoubleSided;

void main() {
    vec3 normal = normalize(vViewNormal);
    if (uDoubleSided != 0 && !gl_FrontFacing) normal = -normal;
    FragPosition = vec4(vViewPosition, 1.0);
    FragNormal = vec4(normal, 1.0);
}
