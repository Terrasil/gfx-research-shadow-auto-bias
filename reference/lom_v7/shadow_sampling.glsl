#ifndef LOM_SHADOW_SAMPLING_GLSL
#define LOM_SHADOW_SAMPLING_GLSL
#include "shadow_common.glsl"
layout(set = 0, binding = 1) uniform sampler2D sunShadowMap;
layout(set = 0, binding = 2) uniform usampler2D sunShadowPlane;

// Unit roundoff for float32, and the worst storage step of the engine's
// D32_SFLOAT / D32_SFLOAT_S8_UINT / D24_UNORM_S8_UINT depth candidates.
// D32 spacing in [0,1) is never larger than the D24 step. No format guessing.
const float SHADOW_ROUNDOFF = 5.960464477539063e-8;
const float SHADOW_DEPTH_STEP = 1.0 / 16777215.0;

float shadowRoundoff(float operations) {
    float error=operations*SHADOW_ROUNDOFF;
    return error/(1.0-error);
}

vec4 shadowAbsoluteProduct(mat4 matrix, vec4 value) {
    return abs(matrix[0])*abs(value.x)+abs(matrix[1])*abs(value.y)
        +abs(matrix[2])*abs(value.z)+abs(matrix[3])*abs(value.w);
}

// Evaluate a camera ray with its R32F reciprocal view depth. The high/low
// matrix keeps arithmetic precise without requiring GPU float64. Forward
// depth near 1 is used for visibility only, never for automatic bias.
vec4 shadowCameraRay(vec4 position, out vec4 error) {
    precise float reciprocalDepth=position.z;
    precise vec4 a=fma(shadow.cameraRayToLight[2],vec4(reciprocalDepth),shadow.cameraRayToLight[3]*position.w);
    precise vec4 b=fma(shadow.cameraRayToLight[0],vec4(position.x),a);
    precise vec4 high=fma(shadow.cameraRayToLight[1],vec4(position.y),b);
    precise vec4 c=fma(shadow.cameraRayToLightLow[2],vec4(reciprocalDepth),shadow.cameraRayToLightLow[3]*position.w);
    precise vec4 d=fma(shadow.cameraRayToLightLow[0],vec4(position.x),c);
    precise vec4 low=fma(shadow.cameraRayToLightLow[1],vec4(position.y),d);
    precise vec4 result=high+low;
    // Cover both a fused implementation and a multiply/add lowering.
    error=shadowRoundoff(1.0)*(abs(a)+abs(b)+abs(high)+abs(c)+abs(d)+abs(low)+abs(result)
        +abs(shadow.cameraRayToLight[2]*reciprocalDepth)+abs(shadow.cameraRayToLight[0]*position.x)
        +abs(shadow.cameraRayToLight[1]*position.y)+abs(shadow.cameraRayToLightLow[2]*reciprocalDepth)
        +abs(shadow.cameraRayToLightLow[0]*position.x)+abs(shadow.cameraRayToLightLow[1]*position.y)
        +shadowAbsoluteProduct(shadow.cameraRayToLightLow,vec4(position.xy,reciprocalDepth,position.w)));
    return result;
}

vec3 shadowCameraDerivative(int column, vec3 ndc) {
    return fma(-ndc,vec3(shadow.cameraRayToLight[column].w),shadow.cameraRayToLight[column].xyz)
        +fma(-ndc,vec3(shadow.cameraRayToLightLow[column].w),shadow.cameraRayToLightLow[column].xyz);
}

struct ShadowReceiverData {
    vec2 gradient;
    vec2 gradientError;
    float planeError;
    vec4 lightClip;
    bool terrain;
    bool valid;
};

// Use the face plane written by the rasterizing primitive, not derivatives
// across neighboring G-buffer pixels that may belong to different triangles.
ShadowReceiverData shadowReceiverData(vec3 position, vec2 screenUV, float depth, float reciprocalDepth, vec2 encodedFace, bool heightField) {
    ShadowReceiverData result=ShadowReceiverData(vec2(0.0),vec2(0.0),0.0,vec4(0.0),heightField,false);
    if(shadow.sampling.w<0.5 || depth>=1.0) return result;
    float halfWidth=shadow.coverage.y, depthRange=shadow.softness.w;
    vec3 rowX=vec3(shadow.lightViewProjection[0].x,shadow.lightViewProjection[1].x,shadow.lightViewProjection[2].x);
    vec3 rowY=vec3(shadow.lightViewProjection[0].y,shadow.lightViewProjection[1].y,shadow.lightViewProjection[2].y);
    vec3 rowZ=vec3(shadow.lightViewProjection[0].z,shadow.lightViewProjection[1].z,shadow.lightViewProjection[2].z);
    // Plane ratios are homogeneous: normalizing the octahedral vector is
    // unnecessary. Bound each encoded SNORM16 component before rotation;
    // a blanket unit-normal error greatly overestimates grazing sun slopes.
    vec3 faceNormal=decodeWorldNormalPlane(encodedFace);
    const float quantization=0.5/32767.0;
    float decodingError=shadowRoundoff(4.0)*(1.0+dot(abs(encodedFace),vec2(1.0)));
    vec3 normalError=vec3(quantization,quantization,2.0*quantization)+vec3(decodingError);
    vec3 basisX=rowX*halfWidth,basisY=rowY*halfWidth,basisZ=rowZ*depthRange;
    vec3 plane=vec3(dot(faceNormal,basisX),dot(faceNormal,basisY),dot(faceNormal,basisZ));
    vec3 planeError=vec3(dot(normalError,abs(basisX)),dot(normalError,abs(basisY)),dot(normalError,abs(basisZ)))
        +shadowRoundoff(5.0)*vec3(dot(abs(faceNormal),abs(basisX)),dot(abs(faceNormal),abs(basisY)),dot(abs(faceNormal),abs(basisZ)));
    float safeDenominator=abs(plane.z)-planeError.z;
    if(safeDenominator<=0.0) return result;
    float scale=halfWidth/depthRange;
    result.gradient=-plane.xy/plane.z*scale;
    result.gradientError=(planeError.xy+abs(plane.xy/plane.z)*planeError.z)/safeDenominator*scale
        +shadowRoundoff(3.0)*abs(result.gradient);
    result.valid=true;
    if(shadow.warpOffset.z>0.5 && (reciprocalDepth<=0.0 || isnan(reciprocalDepth) || isinf(reciprocalDepth))) { result.valid=false;return result; }
    vec4 screenPosition=vec4(screenUV*2.0-1.0,reciprocalDepth,1.0);
    vec4 clipError=vec4(0.0);
    if(shadow.warpOffset.z>0.5) result.lightClip=shadowCameraRay(screenPosition,clipError);
    else {
        result.lightClip=shadowProjectWorld(position);
        return result;
    }
    vec4 inputError=vec4(vec2(shadowRoundoff(3.0)),
        shadowRoundoff(4.0)*abs(reciprocalDepth),0.0);
    vec4 matrixW=vec4(shadow.cameraRayToLight[0].w,shadow.cameraRayToLight[1].w,
        shadow.cameraRayToLight[2].w,shadow.cameraRayToLight[3].w)
        +vec4(shadow.cameraRayToLightLow[0].w,shadow.cameraRayToLightLow[1].w,
        shadow.cameraRayToLightLow[2].w,shadow.cameraRayToLightLow[3].w);
    float denominator=abs(result.lightClip.w)-clipError.w-dot(abs(matrixW),inputError);
    if(denominator<=0.0) { result.valid=false; return result; }
    vec3 lightNdc=result.lightClip.xyz/result.lightClip.w;
    // Input-depth errors in the numerator and denominator are correlated.
    // Propagate their common derivative instead of adding independent bounds.
    vec3 cx=shadowCameraDerivative(0,lightNdc), cy=shadowCameraDerivative(1,lightNdc);
    vec3 cz=shadowCameraDerivative(2,lightNdc);
    vec3 planeWeights=vec3(-result.gradient,1.0);
    vec3 absolutePlane=abs(planeWeights);
    // A depth error moves the reconstructed point along ONE camera ray.
    // Project that correlated displacement onto the receiver plane before
    // taking its magnitude. Summing axis bounds erases thin contacts and
    // changes the margin at triangle edges when the camera grazes a surface.
    vec3 planeDerivatives=vec3(dot(planeWeights,cx),dot(planeWeights,cy),dot(planeWeights,cz));
    vec3 derivativeError=shadowRoundoff(5.0)*vec3(dot(absolutePlane,abs(cx)),
        dot(absolutePlane,abs(cy)),dot(absolutePlane,abs(cz)));
    derivativeError+=vec3(dot(result.gradientError,abs(cx.xy)),
        dot(result.gradientError,abs(cy.xy)),dot(result.gradientError,abs(cz.xy)));
    float inputPlaneError=dot(abs(planeDerivatives)+derivativeError,inputError.xyz);
    precise vec3 relativePosition=position-shadow.worldOrigin.xyz;
    precise vec3 virtualOrigin=position-relativePosition;
    precise vec3 subtractionError=(position-(relativePosition+virtualOrigin))+(virtualOrigin-shadow.worldOrigin.xyz);
    vec4 projectionError=shadowRoundoff(7.0)*shadowAbsoluteProduct(shadow.lightViewProjection,vec4(relativePosition,1.0))
        +shadowAbsoluteProduct(shadow.lightViewProjection,vec4(SHADOW_ROUNDOFF*abs(position)+abs(subtractionError),0.0));
    float planePosition=dot(planeWeights,lightNdc);
    result.planeError=(dot(absolutePlane,clipError.xyz)+abs(planePosition)*clipError.w+inputPlaneError)/denominator
        +SHADOW_ROUNDOFF*abs(planePosition)+dot(absolutePlane,projectionError.xyz);

    result.valid=!isnan(result.planeError) && !isinf(result.planeError)
        && !any(isnan(result.gradientError)) && !any(isinf(result.gradientError));
    return result;
}

bool insideShadowMap(vec2 uv) {
    return all(greaterThanEqual(uv,vec2(0.0))) && all(lessThan(uv,vec2(1.0)));
}

float shadowTapError(vec2 sampleNdc,vec4 gradient) {
    if(shadow.warpOffset.z<=0.5) return 0.0;
    vec2 derivative=max(shadowWarpDerivative(sampleNdc),vec2(1e-6));
    vec2 warped=warpShadowNdc(sampleNdc);
    vec2 uncertainty=shadowRoundoff(5.0)*(vec2(1.0)+abs(sampleNdc)
        +(vec2(1.0)+abs(warped))/derivative);
    return dot(abs(gradient.xy),uncertainty)*shadow.warpOffset.z;
}

vec4 shadowDepthSample(vec2 uv, vec2 ndc, float reference, vec4 gradient, bool terrainReceiver) {
    if(!insideShadowMap(uv)) return vec4(0.0,0.0,-1.0,0.0);
    // Depth is point sampled. Never interpolate depths before comparing them.
    ivec2 texel=min(ivec2(uv/shadow.sampling.x),textureSize(sunShadowMap,0)-1);
    vec2 center=(vec2(texel)+0.5)*shadow.sampling.x;
    vec2 offset=unwarpShadowNdc(center*2.0-1.0)-ndc;
    float depth=texelFetch(sunShadowMap,texel,0).r;
    // A cleared texel contains no caster, even if the receiver plane extrapolates
    // beyond the light's far plane at a grazing angle.
    if(depth>=1.0) return vec4(offset,-1.0,0.0);
    float receiver=reference+dot(gradient.xy,offset);
    // A grazing tangent may leave the light depth volume at an off-center tap.
    // That extrapolated point has no depth representation in this map.
    if(receiver<=0.0 || receiver>=1.0 || isnan(receiver) || isinf(receiver)) return vec4(offset,-1.0,0.0);
    float delta=receiver-depth;
    // Slope uncertainty depends on this tap's actual displacement. Using the
    // entire search radius here would erase small nearby contact casters.
    float tolerance=dot(gradient.zw,abs(offset))+shadowTapError(ndc+offset,gradient);

    return vec4(offset,delta,tolerance);
}

float shadowCompare(vec2 uv, vec2 ndc, float reference, vec4 gradient, bool terrainReceiver) {
    vec4 sampleData=shadowDepthSample(uv,ndc,reference,gradient,terrainReceiver);
    float receiver=reference+dot(gradient.xy,sampleData.xy);
    if(insideShadowMap(uv) && (receiver<=0.0 || receiver>=1.0 || isnan(receiver) || isinf(receiver))) {
        // The unfiltered variant has no other taps to normalize. Fall back
        // to the original query depth, consistently with the filtered path.
        ivec2 pixel=clamp(ivec2(uv/shadow.sampling.x),ivec2(0),textureSize(sunShadowMap,0)-1);
        return step(reference,texelFetch(sunShadowMap,pixel,0).r);
    }
    return sampleData.z<=sampleData.w ? 1.0 : 0.0;
}

// Gather raw depths once, compare each against its own receiver-plane depth,
// then interpolate visibility. Interpolating depths would invent occluders.
vec2 shadowCompareBilinearWeighted(vec2 uv, vec2 ndc, float reference, vec4 gradient, bool terrainReceiver) {
    if(!insideShadowMap(uv)) return vec2(1.0);
    vec2 texel=uv/shadow.sampling.x-0.5;
    ivec2 origin=ivec2(floor(texel));
    vec2 weight=fract(texel);
    // Select the quad at its midpoint. Hardware coordinate quantization must
    // not choose a different quad than the one used for receiver correction.
    vec4 depths=textureGather(sunShadowMap,(vec2(origin)+1.0)*shadow.sampling.x,0);
    vec2 a=unwarpShadowNdc((vec2(origin)+0.5)*shadow.sampling.x*2.0-1.0)-ndc;
    vec2 b=unwarpShadowNdc((vec2(origin)+1.5)*shadow.sampling.x*2.0-1.0)-ndc;
    // GLSL gather order: (x0,y1), (x1,y1), (x1,y0), (x0,y0).
    vec4 dx=vec4(a.x,b.x,b.x,a.x), dy=vec4(b.y,b.y,a.y,a.y);
    vec4 receivers=reference+gradient.x*dx+gradient.y*dy;
    float mappingTolerance=max(shadowTapError(ndc+a,gradient),shadowTapError(ndc+b,gradient));
    vec4 tolerance=gradient.z*abs(dx)+gradient.w*abs(dy)+mappingTolerance;

    vec4 visible=step(receivers-tolerance,depths);
    vec4 represented=vec4(1.0);
    for(int i=0;i<4;++i)
        if(receivers[i]<=0.0 || receivers[i]>=1.0 || isnan(receivers[i]) || isinf(receivers[i])) represented[i]=0.0;
    ivec2 size=textureSize(sunShadowMap,0);
    bvec4 validX=bvec4(origin.x>=0,origin.x+1<size.x,origin.x+1<size.x,origin.x>=0);
    bvec4 validY=bvec4(origin.y+1<size.y,origin.y+1<size.y,origin.y>=0,origin.y>=0);
    for(int i=0;i<4;++i)
        if(!validX[i] || !validY[i] || depths[i]>=1.0) {visible[i]=1.0;represented[i]=1.0;}
    vec4 weights=vec4((1.0-weight.x)*weight.y,weight.x*weight.y,
        weight.x*(1.0-weight.y),(1.0-weight.x)*(1.0-weight.y))*represented;
    return vec2(dot(visible,weights),dot(weights,vec4(1.0)));
}
float shadowResolveComparison(vec2 sum,vec2 uv,float reference) {
    if(sum.y>0.0) return sum.x/sum.y;
    if(!insideShadowMap(uv)) return 1.0;
    // No receiver-plane tap is representable at this grazing angle. Use the
    // original query depth, rather than turning missing taps into sunlight.
    ivec2 pixel=clamp(ivec2(uv/shadow.sampling.x),ivec2(0),textureSize(sunShadowMap,0)-1);
    return step(reference,texelFetch(sunShadowMap,pixel,0).r);
}
float shadowCompareBilinear(vec2 uv,vec2 ndc,float reference,vec4 gradient,bool terrainReceiver) {
    return shadowResolveComparison(shadowCompareBilinearWeighted(uv,ndc,reference,gradient,terrainReceiver),uv,reference);
}

float fixedSunShadow(vec2 uv, vec2 ndc, float reference, vec4 gradient, bool terrainReceiver) {
    int radius=int(shadow.sampling.y+0.5);
    // Preserve an explicitly unfiltered mask when PCF is disabled.
    if(radius==0) return shadowCompare(uv,ndc,reference,gradient,terrainReceiver);
    vec2 visible=vec2(0.0);
    for(int y=-radius;y<=radius;++y) for(int x=-radius;x<=radius;++x)
        visible+=shadowCompareBilinearWeighted(uv+vec2(x,y)*shadow.sampling.x,ndc,reference,gradient,terrainReceiver);
    return shadowResolveComparison(visible,uv,reference);
}

vec2 nextShadowDirection(vec2 direction) {
    // Fixed golden-angle rotation without per-pixel trigonometry.
    return vec2(-0.737368878*direction.x-0.675490294*direction.y,
                 0.675490294*direction.x-0.737368878*direction.y);
}

// Limit penumbrae in the unwarped light plane. A cap in local NDC texels
// would shrink the physical blur precisely where the map has more detail.
// The CPU already combined the texel cap with soft_shadow_max_distance.
float maximumSunShadowRadius() {
    return shadow.softness.z*2.0*shadow.sampling.x;
}

float filterSunRadius(vec2 ndc, float reference, vec4 gradient, float radius, vec2 rotation, float phase, int count, bool terrainReceiver) {
    vec2 visible=vec2(0.0);
    vec2 direction=rotation;
    for(int i=0;i<count;++i) {
        // Sample a disk in linear light space, then warp each tap separately.
        // A disk in warped texture UV would change the physical penumbra in NDC.
        vec2 offset=direction*(sqrt((float(i)+phase)/float(count))*radius);
        vec2 sampleUv=warpShadowNdc(ndc+offset)*0.5+0.5;
        visible+=shadowCompareBilinearWeighted(sampleUv,ndc,reference,gradient,terrainReceiver);
        direction=nextShadowDirection(direction);
    }
    return shadowResolveComparison(visible,warpShadowNdc(ndc)*0.5+0.5,reference);
}

// Deterministic quadrature for a disk smaller than a shadow texel. Its
// center/ring weights reproduce the disk's second moment without random grain.
float filterSmallSunRadius(vec2 ndc,float reference,vec4 gradient,float radius,bool terrainReceiver) {
    vec2 center=warpShadowNdc(ndc)*0.5+0.5;
    vec2 visible=4.0*shadowCompareBilinearWeighted(center,ndc,reference,gradient,terrainReceiver);
    vec2 offset=vec2(radius*0.86602540378,0.0);
    for(int i=0;i<8;++i) {
        visible+=shadowCompareBilinearWeighted(warpShadowNdc(ndc+offset)*0.5+0.5,ndc,reference,gradient,terrainReceiver);
        offset=vec2(offset.x-offset.y,offset.x+offset.y)*0.70710678118;
    }
    return shadowResolveComparison(visible,center,reference);
}

float softSunShadow(vec2 uv, vec2 ndc, float reference, vec4 gradient, float separationBias, bool terrainReceiver, bool adaptive, out float filterRadius) {
    filterRadius=0.0;
    float angularRadius=shadow.softness.x;
    vec2 warpDerivative=max(shadowWarpDerivative(ndc),vec2(1e-6));
    vec2 texelExtent=2.0*shadow.sampling.x/warpDerivative;
    float fineTexel=min(texelExtent.x,texelExtent.y);
    float coarseTexel=max(texelExtent.x,texelExtent.y);
    float texelAllowance=0.5*length(texelExtent);
    float maximumRadius=maximumSunShadowRadius();
    float gapToRadius=shadow.softness.w*angularRadius/shadow.coverage.y;
    float searchRadius=min(max(reference+separationBias,0.0)*gapToRadius,maximumRadius);
    if(searchRadius<=fineTexel*0.5) return shadowCompareBilinear(uv,ndc,reference,gradient,terrainReceiver);

    int count=clamp(int(shadow.softness.y+0.5),8,64);
    int blockerSamples=count;
    // The same light-space quadrature on all receivers. Camera motion
    // cannot rotate a screen-space noise pattern over stationary geometry.
    const float phase=0.5;
    const vec2 rotation=vec2(1.0,0.0);
    vec2 direction=rotation;
    float minimumRadius=fineTexel*0.5;
    float logStep=log2(searchRadius/minimumRadius)/float(blockerSamples-1);
    float searchStep=exp2(logStep);
    float searchDistance=minimumRadius*exp2(phase*logStep);
    float blockerLogGap=0.0;
    float blockers=0.0;
    for(int i=0;i<blockerSamples;++i) {
        // Search equal intervals of log radius, from half a local texel to
        // the full disk. A wide search must retain nearby contact blockers.
        // The final PCF still integrates equal-area annuli, not this density.
        vec2 offset=direction*(i==0 ? 0.0 : searchDistance);
        if(i>0) searchDistance*=searchStep;
        vec2 sampleUv=warpShadowNdc(ndc+offset)*0.5+0.5;
        vec4 sampleData=shadowDepthSample(sampleUv,ndc,reference,gradient,terrainReceiver);
        if(sampleData.z>sampleData.w) {
            float separation=max(sampleData.z+separationBias,1e-8);
            float reach=separation*gapToRadius;
            float influence=1.0-smoothstep(max(reach-texelAllowance,0.0),
                reach+texelAllowance,length(sampleData.xy));
            float threshold=max(sampleData.w*2.0,SHADOW_DEPTH_STEP*2.0);
            influence*=smoothstep(0.0,threshold,sampleData.z-sampleData.w);
            blockerLogGap+=influence*log2(separation);
            blockers+=influence;
        }
        direction=nextShadowDirection(direction);
    }
    if(blockers<=0.0) return shadowCompareBilinear(uv,ndc,reference,gradient,terrainReceiver);

    // Weighted geometric mean: G = exp(sum(weight*log(gap))/sum(weight)).
    // This estimates blocker separation; it does not reconstruct hidden layers.
    float gap=exp2(blockerLogGap/blockers);
    float radius=min(gap*gapToRadius,maximumRadius);
    filterRadius=radius;
    // Bilinear comparisons already antialias the contact. Keep one continuous
    // filter as its radius grows, instead of switching contact/soft masks.
    // Rotate the integration pattern relative to the blocker search.
    vec2 filterRotation=vec2(-rotation.y,rotation.x);
    float radiusInTexels=radius/coarseTexel;
    if(adaptive && radiusInTexels<1.0) {
        float small=filterSmallSunRadius(ndc,reference,gradient,radius,terrainReceiver);
        if(radiusInTexels<=0.5) return small;
        float large=filterSunRadius(ndc,reference,gradient,radius,filterRotation,1.0-phase,count,terrainReceiver);
        return mix(small,large,smoothstep(0.5,1.0,radiusInTexels));
    }
    return filterSunRadius(ndc,reference,gradient,radius,filterRotation,1.0-phase,count,terrainReceiver);
}

float shadowReceiverBias(vec2 ndc, float slope, vec2 gradient) {
    float bias=shadow.parameters.y*(1.0+slope);
    if(shadow.coverage.z>0.0 || (shadow.parameters.x>0.0 && shadow.warpOffset.w>0.0)) {
        vec2 warpDerivative=shadowWarpDerivative(ndc);
        vec2 texelNdc=2.0*shadow.sampling.x/warpDerivative;
        float planeDepth=dot(abs(gradient),texelNdc);
        float texelDepth=max(texelNdc.x,texelNdc.y)*shadow.coverage.y/shadow.softness.w;
        vec2 relative=abs(ndc-shadow.warpCenterGain.xy);
        vec2 growth=vec2(1.0)+shadow.parameters.x*relative;
        float multiplier=mix(1.0,max(growth.x*growth.x,growth.y*growth.y),shadow.warpOffset.w);
        // Retain the base tolerance at the focus. Outside it, grow with the
        // actual NDC footprint, bounded by two local texels' depth uncertainty.
        float localLimit=max(bias,2.0*max(planeDepth,texelDepth));
        bias=min(bias*multiplier,localLimit);
        // Bound both slope and the geometric uncertainty of a coarse texel.
        bias=max(bias,shadow.coverage.z*max(planeDepth,texelDepth));
    }
    return bias;
}

// Bound inverse-warp roundoff over all texel centers touched by this query.
// The bound grows where inverse NDC is ill-conditioned, without a hand-picked
// multiplier proportional to distance from the focus.
vec2 shadowUnwarpError(vec2 minimumUv, vec2 maximumUv, out bool valid) {
    vec2 a=minimumUv*2.0-1.0, b=maximumUv*2.0-1.0;
    vec2 coordinateError=shadowRoundoff(4.0)*(vec2(1.0)+max(abs(a),abs(b)));
    valid=true;
    if(shadow.parameters.x<=0.0) return coordinateError;
    vec2 relative=max(abs(a-shadow.warpOffset.xy),abs(b-shadow.warpOffset.xy));
    vec2 relativeError=coordinateError+SHADOW_ROUNDOFF*(relative+abs(shadow.warpOffset.xy));
    vec2 denominator=shadow.warpCenterGain.zw-shadow.parameters.x*relative;
    vec2 denominatorError=shadowRoundoff(2.0)*(abs(shadow.warpCenterGain.zw)
        +shadow.parameters.x*relative)+shadow.parameters.x*relativeError;
    vec2 safeDenominator=denominator-denominatorError;
    valid=all(greaterThan(safeDenominator,vec2(0.0)));
    if(!valid) return vec2(0.0);
    vec2 quotient=relative/denominator;
    return (relativeError+quotient*denominatorError)/safeDenominator
        +SHADOW_ROUNDOFF*(quotient+abs(shadow.warpCenterGain.xy));
}

bool shadowDecodeCasterGradient(uint encoded, out vec2 gradient) {
    gradient=vec2(0.0);
    if(encoded==0x7fff7fffu) return false;
    vec3 plane=decodeWorldNormalPlane(unpackSnorm2x16(encoded));
    if(abs(plane.z)<=1e-7 || isnan(plane.z) || isinf(plane.z)) return false;
    gradient=-plane.xy/plane.z*(shadow.coverage.y/shadow.softness.w);
    return !any(isnan(gradient)) && !any(isinf(gradient));
}

float shadowAutomaticBias(vec3 ndc, ShadowReceiverData receiver, out bool valid) {
    vec2 uv=warpShadowNdc(ndc.xy)*0.5+0.5;
    float padding=shadow.sampling.x;
    vec2 minimumUv=uv-padding, maximumUv=uv+padding;
    vec2 first=vec2(shadow.sampling.x*0.5), last=vec2(1.0)-first;
    minimumUv=clamp(minimumUv,first,last);
    maximumUv=clamp(maximumUv,first,last);
    vec2 mappingError=shadowUnwarpError(minimumUv,maximumUv,valid);
    if(!valid) return 0.0;
    vec2 footprint=max(abs(unwarpShadowNdc(minimumUv*2.0-1.0)-ndc.xy),
        abs(unwarpShadowNdc(maximumUv*2.0-1.0)-ndc.xy));
    vec2 halfFootprint=footprint*0.5;
    float bias=receiver.planeError+dot(abs(receiver.gradient),mappingError)
        +SHADOW_DEPTH_STEP*2.5;
    // Automatic bias must also cover the geometric depth range represented by
    // one shadow texel. Without this term, distant steep receivers acne even
    // when arithmetic roundoff is bounded correctly.
    float texelDepth=dot(abs(receiver.gradient)+receiver.gradientError,halfFootprint);
    bias+=shadow.coverage.z*texelDepth;
    // If this texel stores a caster plane, cover the local mismatch between
    // the receiver slope and the plane that actually wrote the depth sample.
    ivec2 texel=clamp(ivec2(uv/shadow.sampling.x),ivec2(0),textureSize(sunShadowPlane,0)-1);
    vec2 casterGradient=vec2(0.0);
    if(shadowDecodeCasterGradient(texelFetch(sunShadowPlane,texel,0).r,casterGradient))
        bias+=shadow.coverage.z*dot(abs(receiver.gradient-casterGradient),halfFootprint);
    // Error of the two-product plane evaluation, addition and comparison.
    bias+=shadowRoundoff(5.0)*(abs(ndc.z)+1.0+dot(abs(receiver.gradient),footprint));
    bias*=shadow.warpOffset.z;
    valid=!isnan(bias) && !isinf(bias) && bias>=0.0;
    return bias;
}

float sunShadowFade(vec2 ndc, float viewerDistance) {
    float fade=1.0-smoothstep(shadow.coverage.x,shadow.camera.w,viewerDistance);
    float edge=max(abs(ndc.x),abs(ndc.y));
    if(shadow.coverage.w>0.0) fade*=1.0-smoothstep(1.0-shadow.coverage.w,1.0,edge);
    return fade;
}

float sampleSunShadow(vec3 position, vec3 normal, ShadowReceiverData receiver, bool adaptive, out float filterRadius) {
    filterRadius=0.0;
    if(shadow.sampling.w<0.5) return 1.0;
    float viewerDistance=length(position.xz-shadow.camera.xz);
    if(viewerDistance>=shadow.camera.w) return 1.0;
    vec4 gradient=vec4(receiver.gradient,vec2(0.0));
    bool automatic=shadow.warpOffset.z>0.5 && receiver.valid;
    vec4 clip=automatic ? receiver.lightClip : shadowProjectWorld(position);
    vec3 receiverNdc=clip.xyz/clip.w;
    float bias=0.0;
    if(automatic) bias=shadowAutomaticBias(receiverNdc,receiver,automatic);
    if(!automatic) {
        clip=shadowProjectWorld(position);
        receiverNdc=clip.xyz/clip.w;
    }
    gradient.zw=automatic ? receiver.gradientError*shadow.warpOffset.z : vec2(0.0);
    bool terrainReceiver=automatic && receiver.terrain;
    if(!receiver.valid) {
        vec3 rowX=vec3(shadow.lightViewProjection[0].x,shadow.lightViewProjection[1].x,shadow.lightViewProjection[2].x);
        vec3 rowY=vec3(shadow.lightViewProjection[0].y,shadow.lightViewProjection[1].y,shadow.lightViewProjection[2].y);
        vec3 rowZ=vec3(shadow.lightViewProjection[0].z,shadow.lightViewProjection[1].z,shadow.lightViewProjection[2].z);
        vec3 plane=vec3(dot(normal,rowX),dot(normal,rowY),dot(normal,rowZ));
        if(abs(plane.z)>1e-7) gradient.xy=-plane.xy/plane.z;
    }
    float slope=0.0;
    if(!automatic) {
        float facing=dot(normal,environment.sunDirection.xyz);
        slope=clamp(1.0-abs(facing),0.0,1.0);
        vec3 biasNormal=facing>=0.0 ? normal : -normal;
        // Manual bias only: avoid transforming a normal for a zero offset
        // in the default automatic path.
        clip.xyz+=(shadow.lightViewProjection*vec4(biasNormal*(shadow.parameters.z*slope),0.0)).xyz;
    }
    vec3 ndc=clip.xyz/clip.w;
    if(ndc.z<=0.0 || ndc.z>=1.0 || any(greaterThan(abs(ndc.xy),vec2(1.0)))) return 1.0;
    vec2 uv=warpShadowNdc(ndc.xy)*0.5+0.5;
    if(!automatic) bias=shadowReceiverBias(ndc.xy,slope,gradient.xy);
    float reference=ndc.z-bias;
    // Bias classifies occlusion; it must not shorten the physical PCSS separation.
    float separationBias=max(receiverNdc.z-ndc.z+bias+dot(gradient.xy,ndc.xy-receiverNdc.xy),0.0);
    float visible=shadow.sampling.z>0.5 && shadow.softness.x>0.0 && receiver.valid
        ? softSunShadow(uv,ndc.xy,reference,gradient,separationBias,terrainReceiver,adaptive,filterRadius) : fixedSunShadow(uv,ndc.xy,reference,gradient,terrainReceiver);
    if(shadow.sampling.z<0.5) {
        vec2 texel=2.0*shadow.sampling.x/max(shadowWarpDerivative(ndc.xy),vec2(1e-6));
        filterRadius=float(shadow.sampling.y)*max(texel.x,texel.y);
    }
    float fade=sunShadowFade(ndc.xy,viewerDistance);
    return 1.0-(1.0-visible)*shadow.parameters.w*fade;
}

#endif
