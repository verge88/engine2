#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uLightSpace;

out vec3 vWorldPosition;
out vec2 vUV;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    vUV = aUV;
    gl_Position = uLightSpace * world;
}
