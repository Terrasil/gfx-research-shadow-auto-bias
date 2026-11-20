#version 460 core

in vec3 worldPosition;
flat in vec3 worldNormal;
flat in float isTerrain;

uniform vec3 lightU, lightV, sunDirection;
uniform float lightHalf, depthRange;

layout(location = 0) out vec4 receiverPosition;
layout(location = 1) out vec4 receiverGradient;
layout(location = 2) out float receiverError;
layout(location = 3) out vec4 shadedSurface;

float gammaN(float operations) {
    float u = operations * 5.960464477539063e-8;
    return u / (1.0 - u);
}

vec3 toLightNormal(vec3 n) {
    return vec3(dot(n, lightU), dot(n, lightV), -dot(n, sunDirection));
}

vec4 screenGradient(vec3 n) {
    // The diagnostic benchmark uses an octahedral SNORM16 receiver normal.
    vec3 oct = n / dot(abs(n), vec3(1.0));
    if (oct.z < 0.0) {
        vec2 signs = mix(vec2(-1.0), vec2(1.0), greaterThanEqual(oct.xy, vec2(0.0)));
        oct.xy = (1.0 - abs(oct.yx)) * signs;
    }
    vec2 e = unpackSnorm2x16(packSnorm2x16(oct.xy));
    float z = 1.0 - abs(e.x) - abs(e.y);
    float t = clamp(-z, 0.0, 1.0);
    vec3 decoded = vec3(e + mix(vec2(t), vec2(-t), greaterThanEqual(e, vec2(0.0))), z);
    vec3 local = toLightNormal(decoded);
    vec3 absoluteNormal = abs(decoded);
    float arithmeticError = gammaN(4.0) * (1.0 + abs(e.x) + abs(e.y));
    vec3 normalError = vec3(0.5, 0.5, 1.0) / 32767.0 + arithmeticError;
    mat3 axes = mat3(abs(lightU), abs(lightV), abs(sunDirection));
    vec3 axisError = (normalError + gammaN(5.0) * absoluteNormal) * axes;
    float scale = lightHalf / depthRange;
    float lowerZ = abs(local.z) - axisError.z;
    if (lowerZ <= 0.0) return vec4(0.0);
    vec2 gradient = -scale * local.xy / local.z;
    vec2 error = scale * (axisError.xy + abs(local.xy / local.z) * axisError.z) / lowerZ;
    return vec4(gradient, error + gammaN(3.0) * abs(gradient));
}

void main() {
    vec3 p = worldPosition;
    vec3 n = worldNormal;
    float cosine = dot(n, sunDirection);
    vec3 lightPosition = vec3(dot(p, lightU) / lightHalf, dot(p, lightV) / lightHalf,
                             0.5 - dot(p, sunDirection) / depthRange);
    bool valid = cosine >= 0.05 && all(lessThan(abs(lightPosition.xy), vec2(0.995)))
        && lightPosition.z > 0.0 && lightPosition.z < 1.0;
    receiverPosition = vec4(lightPosition, valid ? 2.0 : 1.0);
    receiverGradient = valid ? screenGradient(n) : vec4(0.0);
    // Allow for float projection and rounding in the interpolated position.
    vec3 a = abs(p);
    vec2 xyError = gammaN(8.0) * (vec2(1.0) + vec2(dot(a, abs(lightU)), dot(a, abs(lightV))) / lightHalf);
    float zError = gammaN(8.0) * (0.5 + dot(a, abs(sunDirection)) / depthRange);
    receiverError = zError + dot(abs(receiverGradient.xy), xyError);

    vec3 low = vec3(0.34, 0.43, 0.31);
    vec3 high = vec3(0.75, 0.70, 0.51);
    float height = smoothstep(-7.0, 32.0, p.y);
    float stone = smoothstep(0.10, 0.57, 1.0 - n.y);
    vec3 albedo = mix(low, high, height);
    albedo = mix(albedo, vec3(0.46, 0.43, 0.36), stone * 0.76);
    // Low amplitude detail leaves shadow acne and contact errors legible.
    float mottling = sin(p.x * 0.23 + sin(p.z * 0.13)) * sin(p.z * 0.26 - p.x * 0.11);
    albedo *= 1.0 + 0.055 * mottling;
    if (isTerrain < 0.5) albedo = vec3(0.87, 0.63, 0.36);
    float diffuse = sqrt(max(cosine, 0.0));
    float ambient = 0.43 + 0.09 * max(n.y, 0.0);
    vec3 color = albedo * (ambient + (1.0 - ambient) * diffuse);
    shadedSurface = vec4(color, diffuse);
}
