#ifndef LOM_SHADOW_COMMON_GLSL
#define LOM_SHADOW_COMMON_GLSL
layout(std430, set = 0, binding = 9) readonly buffer SunShadowData {
    mat4 lightViewProjection;
    vec4 parameters;
    vec4 sampling;
    vec4 camera;
    vec4 coverage;
    vec4 softness;
    vec4 warpCenterGain;
    vec4 warpOffset;
    mat4 cameraRayToLight;
    mat4 cameraRayToLightLow;
    vec4 worldOrigin;
} shadow;
vec4 shadowProjectWorld(vec3 position) {
    precise vec3 relative=position-shadow.worldOrigin.xyz;
    return shadow.lightViewProjection*vec4(relative,1.0);
}
vec2 warpShadowNdc(vec2 ndc) {
    float strength = shadow.parameters.x;
    if(strength<=0.0) return ndc;
    vec2 relative=ndc-shadow.warpCenterGain.xy;
    return relative*shadow.warpCenterGain.zw/(vec2(1.0)+strength*abs(relative))+shadow.warpOffset.xy;
}
vec2 unwarpShadowNdc(vec2 ndc) {
    float strength = shadow.parameters.x;
    if(strength<=0.0) return ndc;
    vec2 relative=ndc-shadow.warpOffset.xy;
    return relative/(shadow.warpCenterGain.zw-strength*abs(relative))+shadow.warpCenterGain.xy;
}
vec2 shadowWarpDerivative(vec2 ndc) {
    vec2 denominator=vec2(1.0)+shadow.parameters.x*abs(ndc-shadow.warpCenterGain.xy);
    return shadow.warpCenterGain.zw/(denominator*denominator);
}
vec4 shadowClipPosition(vec3 position, out vec3 relativeLightPosition) {
    vec4 clip = shadowProjectWorld(position);
    // Interpolate small light-space coordinates, not absolute world positions.
    // Subtract the orthographic center before interpolation to preserve slopes.
    relativeLightPosition = clip.xyz / clip.w - vec3(shadow.warpCenterGain.xy, 0.5);
    clip.xy = warpShadowNdc(clip.xy / clip.w) * clip.w;
    return clip;
}
#endif
