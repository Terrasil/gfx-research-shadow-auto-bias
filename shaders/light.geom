#version 460 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

uniform float alpha;
uniform int mapSize;

in LightVertex {
    vec3 point;
    vec3 normal;
    float terrain;
} vertices[];

flat out vec3 corner, edgeU, edgeV;
flat out vec3 lightNormal;
flat out float isTerrain;
flat out float inverseArea;

vec2 warp(vec2 p) {
    return (1.0 + alpha) * p / (1.0 + alpha * abs(p));
}

float det2(vec2 u, vec2 v) {
    return u.x * v.y - u.y * v.x;
}

void main() {
    vec3 a = vertices[0].point;
    vec3 u = vertices[1].point - a;
    vec3 v = vertices[2].point - a;
    float area = det2(u.xy, v.xy);
    if (abs(area) <= 1e-12) return;

    // The warp is monotone on each axis. Its corner extrema bound the entire
    // warped triangle, including the curved edges between those corners.
    vec2 padding = vec2(1.0 / float(mapSize));
    vec2 low = warp(min(a.xy, min(vertices[1].point.xy, vertices[2].point.xy))) - padding;
    vec2 high = warp(max(a.xy, max(vertices[1].point.xy, vertices[2].point.xy))) + padding;
    for (int i = 0; i < 4; ++i) {
        // EmitVertex leaves all output values undefined, so write them per vertex.
        corner = a;
        edgeU = u;
        edgeV = v;
        lightNormal = vertices[0].normal;
        isTerrain = vertices[0].terrain;
        inverseArea = 1.0 / area;
        gl_Position = vec4((i & 1) == 0 ? low.x : high.x, i < 2 ? low.y : high.y, 0.0, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
