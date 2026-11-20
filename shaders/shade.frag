#version 460 core
uniform sampler2D mask, positions, base;
uniform int view;
out vec4 color;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 data = texelFetch(mask,p,0).rg;
    vec3 rgb = texelFetch(base,p,0).rgb;
    float kind = texelFetch(positions,p,0).w;
    vec3 result = rgb*(1.0-0.69*data.r);
    if (view == 1) {
        result = vec3(dot(rgb,vec3(0.083)));
        if (kind == 2.0 && data.r >= 0.5) result = vec3(1.0,0.4118,0.2824);
        if (kind >= 3.0 && data.r < 0.5) result = vec3(0.3373,0.8275,0.9333);
    }
    if (view == 2) result = vec3(1.0-data.r)*step(1.0,kind);
    if (view == 3) {
        vec3 cold = mix(vec3(0.076,0.11,0.18),vec3(0.20,0.72,0.70),smoothstep(0.0,0.56,data.g));
        result = mix(cold,vec3(1.0,0.68,0.31),smoothstep(0.56,1.0,data.g));
        if (kind < 2.0) result = vec3(0.055,0.079,0.085);
    }
    color = vec4(result,1.0);
}
