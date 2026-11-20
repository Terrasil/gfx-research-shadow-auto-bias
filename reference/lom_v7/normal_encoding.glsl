#ifndef LOM_NORMAL_ENCODING_GLSL
#define LOM_NORMAL_ENCODING_GLSL

// Signed octahedral pairs: shading normal RG, primitive face normal BA
// in the RGBA16_SNORM G-buffer attachment.
vec2 encodeWorldNormal(vec3 normal) {
    normal /= abs(normal.x) + abs(normal.y) + abs(normal.z);
    vec2 signXY = vec2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
    return normal.z >= 0.0 ? normal.xy : (1.0 - abs(normal.yx)) * signXY;
}

vec3 decodeWorldNormalPlane(vec2 encoded) {
    vec3 normal = vec3(encoded, 1.0 - abs(encoded.x) - abs(encoded.y));
    float correction = clamp(-normal.z, 0.0, 1.0);
    normal.xy += vec2(normal.x >= 0.0 ? -correction : correction,
                      normal.y >= 0.0 ? -correction : correction);
    return normal;
}
vec3 decodeWorldNormal(vec2 encoded) {
    return normalize(decodeWorldNormalPlane(encoded));
}

#endif
