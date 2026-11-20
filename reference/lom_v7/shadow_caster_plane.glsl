#ifndef LOM_SHADOW_CASTER_PLANE_GLSL
#define LOM_SHADOW_CASTER_PLANE_GLSL
#include "shadow_common.glsl"

// Two SNORM16 components fit in one R32_UINT texel for one gather.
// Terrain uses the upper octahedral hemisphere; mesh/foliage use the
// other hemisphere and retain the regular receiver-plane comparison.
// Flipping the plane normal does not change its depth gradient.
uint shadowEncodeCasterPlane(vec3 plane, bool terrain) {
    if(!terrain) return 0x7fff7fffu;
    plane*=vec3(1.0/shadow.coverage.y,1.0/shadow.coverage.y,1.0/shadow.softness.w);
    if(plane.z<0.0) plane=-plane;
    float sum=dot(abs(plane),vec3(1.0));
    return sum>0.0 ? packSnorm2x16(plane.xy/sum) : 0x7fff7fffu;
}
#endif
