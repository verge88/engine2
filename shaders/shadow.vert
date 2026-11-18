#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uLightSpace;

out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = uLightSpace * uModel * vec4(aPosition, 1.0);
}
