#version 300 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp usampler2D;

uniform sampler2D positions, gradients, inputError, depthSlope;
uniform usampler2D casterPlanes;
uniform int mapSize, radius;
uniform float alpha, parameter;
out vec4 color;

float gammaN(float operations) {
    float u = operations * 5.960464477539063e-8;
    return u / (1.0-u);
}

vec2 warp(vec2 p) {
    return ((1.0+alpha)*p)/(1.0+alpha*abs(p));
}

vec2 inverseWarp(vec2 q) {
    return q/(1.0+alpha-alpha*abs(q));
}

vec2 warpDerivative(vec2 p) {
    vec2 d = 1.0+alpha*abs(p);
    return (1.0+alpha)/(d*d);
}

vec2 inverseError(vec2 lo, vec2 hi) {
    vec2 m = max(abs(lo),abs(hi));
    vec2 qe = gammaN(4.0)*(1.0+m);
    if (alpha == 0.0) return qe;
    vec2 re = qe+gammaN(1.0)*m;
    vec2 denom = 1.0+alpha-alpha*m;
    vec2 de = gammaN(2.0)*(1.0+alpha+alpha*m)+alpha*re;
    vec2 q = m/denom;
    return (re+q*de)/(denom-de)+gammaN(1.0)*q;
}

vec2 casterGradient(ivec2 pixel, out bool valid) {
    uint packedPlane = texelFetch(casterPlanes,pixel,0).r;
    vec2 e = unpackSnorm2x16(packedPlane);
    float z = 1.0-abs(e.x)-abs(e.y);
    valid = packedPlane != 0x7fff7fffu && abs(z) > 1e-7;
    float correction = clamp(-z,0.0,1.0);
    e += mix(vec2(correction),vec2(-correction),greaterThanEqual(e,vec2(0.0)));
    return valid ? -0.25*e/z : vec2(0.0);
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 pos = texelFetch(positions,pixel,0);
    color = vec4(0.0,0.0,0.0,1.0);
    if (pos.w < 2.0) return;
#if METHOD == 6
    color.r = step(3.0,pos.w);
#else
    float cell = 1.0/float(mapSize);
    vec2 uv = warp(pos.xy)*0.5+0.5;
    ivec2 center = clamp(ivec2(uv/cell),ivec2(0),ivec2(mapSize-1));
#if METHOD >= 2
    vec4 g = texelFetch(gradients,pixel,0);
#endif
#if METHOD >= 3
    vec2 low = clamp(uv-cell,0.5*cell,1.0-0.5*cell);
    vec2 high = clamp(uv+cell,0.5*cell,1.0-0.5*cell);
    vec2 f = max(abs(inverseWarp(2.0*low-1.0)-pos.xy),abs(inverseWarp(2.0*high-1.0)-pos.xy));
    vec2 eta = inverseError(2.0*low-1.0,2.0*high-1.0);
    float bias = texelFetch(inputError,pixel,0).r+dot(abs(g.xy),eta)+2.5/16777215.0
               + gammaN(5.0)*(pos.z+1.0+dot(abs(g.xy),f));
#if METHOD >= 4
    bias += 0.9*dot(abs(g.xy)+g.zw,f*0.5);
#endif
#if METHOD == 5
    bool valid;
    vec2 caster = casterGradient(center,valid);
    if (valid) bias += 0.9*dot(abs(g.xy-caster),f*0.5);
#endif
    bias *= parameter;
#elif METHOD == 1
    float bias = 0.000002;
#else
    float bias = parameter;
#endif
    int shadow = 0;
    float shownBias = bias;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            ivec2 tap = center+ivec2(x,y);
            if (any(lessThan(tap,ivec2(0))) || any(greaterThanEqual(tap,ivec2(mapSize)))) continue;
            vec3 depths = texelFetch(depthSlope,tap,0).rgb;
            if (depths.x >= 1.0) continue;
#if METHOD > 0
            vec2 p = inverseWarp((vec2(tap)+0.5)*cell*2.0-1.0);
#endif
            float z = pos.z;
            float allowance = bias;
#if METHOD == 1
            vec2 s = abs(depths.yz)/warpDerivative(p);
            allowance += parameter*2.0*cell*max(s.x,s.y);
#if DIAGNOSTICS == 1
            if (x == 0 && y == 0) shownBias = allowance;
#endif
#elif METHOD >= 2
            vec2 offset = p-pos.xy;
            float shifted = z+dot(g.xy,offset);
            if (shifted > 0.0 && shifted < 1.0) z = shifted;
#if METHOD >= 3
            vec2 uncertainty = gammaN(5.0)*(1.0+abs(p)+(1.0+abs(warp(p)))/warpDerivative(p));
            allowance += parameter*(dot(g.zw,abs(offset))+dot(abs(g.xy),uncertainty));
#endif
#endif
            if (z-depths.x > allowance) ++shadow;
        }
    }
    float side = float(2*radius+1);
    color.r = float(shadow)/(side*side);
#if DIAGNOSTICS == 1
    color.g = clamp((log2(max(shownBias,1e-9))*0.301029996+7.0)/5.0,0.0,1.0);
#endif
#endif
}
