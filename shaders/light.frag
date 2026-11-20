#version 460 core

uniform float alpha, lightHalf, depthRange;
uniform int mapSize;

flat in vec3 corner, edgeU, edgeV;
flat in vec3 lightNormal;
flat in float isTerrain;
flat in float inverseArea;

layout(location = 0) out vec4 depthSlope;
layout(location = 1) out uint plane;

float det2(vec2 u, vec2 v) {
    return u.x * v.y - u.y * v.x;
}

void main() {
    vec2 q = gl_FragCoord.xy * (2.0 / float(mapSize)) - 1.0;
    vec2 p = q / (1.0 + alpha - alpha * abs(q));
    vec2 delta = p - corner.xy;
    vec2 bary = vec2(det2(delta, edgeV.xy), det2(edgeU.xy, delta)) * inverseArea;
    if (any(lessThan(bary, vec2(0.0))) || bary.x + bary.y > 1.0) discard;
    float z = corner.z + dot(bary, vec2(edgeU.z, edgeV.z));
    if (z <= 0.0 || z >= 1.0) discard;

    // Write D24-quantized depth and the corresponding nearest caster data.
    float depth = roundEven(z * 16777215.0) / 16777215.0;
    gl_FragDepth = depth;
    vec3 n = lightNormal;
    vec2 gradient = abs(n.z) > 1e-7 ? -(lightHalf / depthRange) * n.xy / n.z : vec2(0.0);
    depthSlope = vec4(depth, gradient, 0.0);
    if (n.z < 0.0) n = -n;
    plane = isTerrain > 0.5 ? packSnorm2x16(n.xy / dot(abs(n), vec3(1.0))) : 0x7fff7fffu;
}
