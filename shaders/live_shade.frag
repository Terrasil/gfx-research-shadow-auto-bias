#version 460 core

uniform sampler2D positions, surface, visibility, depthSlope;
uniform mat4 inverseViewProjection;
uniform vec3 eye, sunDirection;
uniform vec2 resolution;
uniform int displayMode, splitX;

out vec4 color;

vec3 biasColor(float value) {
    vec3 dark = vec3(0.055, 0.11, 0.26);
    vec3 blue = vec3(0.09, 0.66, 0.78);
    vec3 gold = vec3(0.99, 0.72, 0.25);
    return mix(mix(dark, blue, smoothstep(0.0, 0.55, value)), gold, smoothstep(0.55, 1.0, value));
}

vec3 background(vec2 uv) {
    vec4 p = inverseViewProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 ray = normalize(p.xyz / p.w - eye);
    float zenith = smoothstep(-0.82, 0.23, ray.y);
    vec3 deep = vec3(0.095, 0.131, 0.153);
    vec3 mist = vec3(0.25, 0.32, 0.36);
    float glow = pow(max(dot(ray, sunDirection), 0.0), 8.0);
    return mix(deep, mist, zenith) + vec3(0.09, 0.065, 0.035) * glow;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec2 uv = gl_FragCoord.xy / resolution;
    vec4 receiver = texelFetch(positions, pixel, 0);
    vec3 c = background(uv);
    if (displayMode == 3) {
        float size = min(resolution.x, resolution.y) * 0.91;
        vec2 mapUV = (gl_FragCoord.xy - resolution * vec2(0.59, 0.50)) / size + 0.5;
        if (all(greaterThanEqual(mapUV, vec2(0.0))) && all(lessThan(mapUV, vec2(1.0)))) {
            float z = texture(depthSlope, mapUV).r;
            float value = z < 1.0 ? clamp((0.75 - z) * 1.8, 0.0, 1.0) : 0.055;
            c = vec3(value);
        }
    } else if (receiver.w > 0.0) {
        vec4 base = texelFetch(surface, pixel, 0);
        vec2 shadow = texelFetch(visibility, pixel, 0).rg;
        c = base.rgb * (1.0 - shadow.r * (0.38 + 0.32 * base.a));
        if (displayMode == 1) c = vec3(0.12 + 0.76 * (1.0 - shadow.r));
        if (displayMode == 2) c = receiver.w >= 2.0 ? biasColor(shadow.g) : vec3(0.19);
    }
    if (splitX >= 0 && displayMode != 3 && pixel.x == splitX)
        c = mix(c, vec3(0.68, 0.76, 0.84), 0.55);
    color = vec4(c, 1.0);
}
