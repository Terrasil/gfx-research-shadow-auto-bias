#version 460 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float terrain;

uniform vec3 lightU, lightV, sunDirection;
uniform float lightHalf, depthRange;

out LightVertex {
    vec3 point;
    vec3 normal;
    float terrain;
} vertex;

void main() {
    vec3 p = position;
    vertex.point = vec3(dot(p, lightU) / lightHalf, dot(p, lightV) / lightHalf,
                        0.5 - dot(p, sunDirection) / depthRange);
    vertex.normal = vec3(dot(normal, lightU), dot(normal, lightV), -dot(normal, sunDirection));
    vertex.terrain = terrain;
    gl_Position = vec4(vertex.point, 1.0);
}
