#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float terrain;

uniform mat4 viewProjection;

out vec3 worldPosition;
flat out vec3 worldNormal;
flat out float isTerrain;

void main() {
    worldPosition = position;
    worldNormal = normal;
    isTerrain = terrain;
    gl_Position = viewProjection * vec4(position, 1.0);
}
