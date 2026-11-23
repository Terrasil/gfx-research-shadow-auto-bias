#pragma once

#include "scenes.hpp"

#include <numbers>

namespace lab {

constexpr float light_half = 128.0f, depth_range = 512.0f;
constexpr float depth_step = 1.0f / 16777215.0f;
constexpr float footprint_weight = 0.9f;
constexpr std::uint32_t no_plane = 0x7fff7fffu;

struct XY {
    float x{}, y{};
    XY operator+(XY v) const { return {x+v.x,y+v.y}; }
    XY operator-(XY v) const { return {x-v.x,y-v.y}; }
    XY operator*(float s) const { return {x*s,y*s}; }
};
inline float dot(XY a, XY b) { return a.x*b.x+a.y*b.y; }
inline XY abs_xy(XY v) { return {std::abs(v.x),std::abs(v.y)}; }
inline float gamma(float n) {
    const float error = n * 0x1p-24f;
    return error/(1.0f-error);
}

struct Light {
    Vec to_sun, u, v;
    Light(int elevation, int azimuth) {
        const double factor = std::numbers::pi/180.0;
        const double el = elevation*factor, az = azimuth*factor;
        to_sun = {std::cos(el)*std::cos(az), std::sin(el), std::cos(el)*std::sin(az)};
        u = normalized(cross({0,1,0}, to_sun));
        v = cross(to_sun,u);
    }
    Vec project(Vec p) const {
        return {dot(p,u)/light_half, dot(p,v)/light_half, 0.5-dot(p,to_sun)/depth_range};
    }
    Vec near_point(double x, double y) const {
        return u*(x*light_half)+v*(y*light_half)+to_sun*(depth_range*0.5);
    }
    Vec normal_in_light(Vec p) const {
        return {dot(p,u),dot(p,v),-dot(p,to_sun)};
    }
};

template<typename T> T warp(T p, int alpha) {
    return ((T(1)+T(alpha))*p)/(T(1)+T(alpha)*std::abs(p));
}

template<typename T> T unwarp(T q, int alpha) {
    return q/(T(1)+T(alpha)-T(alpha)*std::abs(q));
}

inline float warp_derivative(float p, int alpha) {
    const float v = 1.0f + alpha*std::abs(p);
    return (1.0f+alpha)/(v*v);
}

inline XY gradient(Vec n) {
    const double scale = -static_cast<double>(light_half)/depth_range;
    if (std::abs(n.z) <= 1e-7) return {};
    return {static_cast<float>(scale*n.x/n.z), static_cast<float>(scale*n.y/n.z)};
}

inline int snorm(float x) { return static_cast<int>(std::round(std::clamp(x,-1.0f,1.0f)*32767.0f)); }
inline XY encode_normal(Vec n) {
    n = n*(1.0/(std::abs(n.x)+std::abs(n.y)+std::abs(n.z)));
    const auto d = [](double value) { return value < 0.0 ? -1.0 : 1.0; };
    return n.z >= 0 ? XY{static_cast<float>(n.x), static_cast<float>(n.y)}
        : XY{static_cast<float>((1.0-std::abs(n.y))*d(n.x)),
             static_cast<float>((1.0-std::abs(n.x))*d(n.y))};
}

inline Vec decode_normal(XY e) {
    const float z = 1.0f-std::abs(e.x)-std::abs(e.y);
    const float correction = std::clamp(-z, 0.0f, 1.0f);
    return {e.x+(e.x >= 0 ? -correction : correction),
            e.y+(e.y >= 0 ? -correction : correction), z};
}

inline std::uint32_t encode_caster(Vec light_normal) {
    if (light_normal.z < 0) light_normal = light_normal*(-1.0);
    const XY e = encode_normal(light_normal);
    const auto x = static_cast<std::uint16_t>(snorm(e.x));
    const auto y = static_cast<std::uint16_t>(snorm(e.y));
    return x | (static_cast<std::uint32_t>(y) << 16);
}

inline bool decode_caster(std::uint32_t bits, XY& g) {
    if (bits == no_plane) return false;
    const auto unpack = [](std::uint32_t pair) {
        const int value = static_cast<int>(pair & 65535u);
        return static_cast<float>(value < 32768 ? value : value-65536)/32767.0f;
    };
    const Vec n = decode_normal({unpack(bits),unpack(bits >> 16)});
    if (std::abs(n.z) <= 1e-7) return false;
    g = gradient(n);
    return true;
}

struct Texel {
    float depth = 1.0f;
    XY g{};
    std::uint32_t plane = no_plane;
};

struct ShadowMap {
    int size, alpha;
    std::vector<Texel> texels;

    ShadowMap(const Bvh& mesh, const Light& light, int n, int warp_strength, int jobs)
        : size(n), alpha(warp_strength), texels(n*n) {
        parallel_rows(n, jobs, [&](int j) {
            const double y = unwarp((2.0*j+1.0)/n-1.0, alpha);
            for (int i = 0; i < n; ++i) {
                const double x = unwarp((2.0*i+1.0)/n-1.0, alpha);
                const auto hit = mesh.intersect(Ray(light.near_point(x,y),light.to_sun*(-1.0)),0.0,depth_range);
                if (hit.triangle < 0) continue;
                const auto& tri = mesh.triangles[hit.triangle];
                const auto normal = light.normal_in_light(tri.normal);
                const float quantized = static_cast<float>(std::round(hit.distance/depth_range * 16777215.0)/16777215.0);
                texels[j*n+i] = {quantized, gradient(normal), tri.object == 0 ? encode_caster(normal) : no_plane};
            }
        });
    }

    const Texel& at(int x, int y) const { return texels[y*size+x]; }
};

struct Receiver {
    XY p{}, g{}, error{};
    float z{}, plane_error{};
    std::uint8_t category{}; // 0 sky, 1 excluded, 2 lit, 3 occluded, 4 occluded contact
    std::array<std::uint8_t,4> rgb{};
};

inline double absolute_product(Vec a, Vec b) {
    return std::abs(a.x*b.x)+std::abs(a.y*b.y)+std::abs(a.z*b.z);
}

inline void receiver_plane(Receiver& r, const Light& light, Vec surface_normal) {
    XY pair = encode_normal(surface_normal);
    pair = {snorm(pair.x)/32767.0f,snorm(pair.y)/32767.0f};
    const Vec decoded = decode_normal(pair);
    const Vec n = light.normal_in_light(decoded);
    r.g = gradient(n);
    const double error = gamma(4.0f)*(1.0+std::abs(pair.x)+std::abs(pair.y));
    const double q = 0.5/32767.0;
    const Vec normal_error{q+error,q+error,2.0*q+error};
    auto bound = [&](Vec b) {
        return absolute_product(normal_error,b)+gamma(5.0f)*absolute_product(decoded,b);
    };
    const double ex = bound(light.u), ey = bound(light.v), ez = bound(light.to_sun);
    const double minimum = std::abs(n.z)-ez;
    if (minimum <= 0.0) return;
    const double scale = static_cast<double>(light_half)/depth_range;
    r.error = {
        static_cast<float>((ex+std::abs(n.x/n.z)*ez)/minimum*scale+gamma(3.0f)*std::abs(r.g.x)),
        static_cast<float>((ey+std::abs(n.y/n.z)*ez)/minimum*scale+gamma(3.0f)*std::abs(r.g.y))
    };
}

struct Frame {
    int width, height;
    std::vector<Receiver> pixels;
};

inline Frame make_frame(const Bvh& mesh, const Light& light, int width, int height,
                        bool contact_view, int jobs, double ray_epsilon = 1e-6,
                        Vec view_direction = {-0.48,-0.81,-0.33}, double zoom = 1.0) {
    Frame frame{width,height,std::vector<Receiver>(width*height)};
    const Vec target = contact_view ? Vec{-77,6,72} : Vec{0,4,0};
    const Vec forward = normalized(view_direction);
    const Vec camera = target-forward*350.0;
    const Vec horizontal = normalized(cross(forward,{0,1,0}));
    const Vec vertical = cross(horizontal,forward);
    const double scale = (contact_view ? 24.0 : 132.0)/zoom;
    parallel_rows(height,jobs,[&](int y) {
        for (int x = 0; x < width; ++x) {
            Receiver& r = frame.pixels[y*width+x];
            const Vec origin = camera+horizontal*((2.0*x+1.0-width)*scale/height)
                +vertical*((2.0*y+1.0-height)*scale/height);
            const Hit hit = mesh.intersect(Ray(origin,forward),0.0,1000.0);
            if (hit.triangle < 0) {
                const int shade = 26 + 12*y/height;
                r.rgb = {static_cast<std::uint8_t>(shade),static_cast<std::uint8_t>(shade+8),
                         static_cast<std::uint8_t>(shade+12),255};
                continue;
            }
            const auto& tri = mesh.triangles[hit.triangle];
            const Vec pos = origin+forward*hit.distance;
            const Vec p = light.project(pos);
            const auto cosine = dot(tri.normal,light.to_sun);
            const float diffuse = static_cast<float>(std::max(0.0,cosine));
            if (tri.object == 0) {
                // Use a smooth height palette so erroneous silhouettes and bands remain legible.
                const double factor = std::clamp((pos.y+11.0)/39.0,0.0,1.0);
                const double shade = 0.57+0.43*std::sqrt(diffuse);
                r.rgb = {static_cast<std::uint8_t>(std::lerp(137.0,219.0,factor)*shade),
                         static_cast<std::uint8_t>(std::lerp(159.0,203.0,factor)*shade),
                         static_cast<std::uint8_t>(std::lerp(130.0,170.0,factor)*shade),255};
            } else {
                const double shade = 0.48+0.52*std::sqrt(diffuse);
                r.rgb = {static_cast<std::uint8_t>(239*shade),static_cast<std::uint8_t>(183*shade),
                         static_cast<std::uint8_t>(126*shade),255};
            }
            r.category = 1;
            if (cosine < 0.05 || std::max(std::abs(p.x),std::abs(p.y)) >= 0.995 || p.z <= 0 || p.z >= 1)
                continue;
            r.p = {static_cast<float>(p.x),static_cast<float>(p.y)};
            r.z = static_cast<float>(p.z);
            receiver_plane(r,light,tri.normal);
            // Bound the receiver-depth error introduced by the float32 upload.
            r.plane_error = static_cast<float>(std::abs(p.z-r.z)
                +std::abs((p.x-r.p.x)*r.g.x)+std::abs((p.y-r.p.y)*r.g.y));
            const Hit blocker = mesh.intersect(Ray(pos,light.to_sun),ray_epsilon,depth_range,hit.triangle);
            r.category = blocker.triangle >= 0 ? 3 : 2;
            if (tri.object == 0 && blocker.triangle >= 0) {
                const int object = mesh.triangles[blocker.triangle].object;
                if (object > 0 && boxes[object-1].horizontal_distance(pos) <= 2.0) r.category = 4;
            }
        }
    });
    return frame;
}

enum class Method { constant, slope, receiver, numeric, footprint, full, selective, reference };
inline constexpr std::array method_names{"constant","slope","receiver","numeric","footprint","full","selective","reference"};
inline constexpr std::array default_params{0.0001f,0.5f,0.000002f,2.0f,2.0f,2.0f,2.0f,0.0f};
inline constexpr float slope_floor = 0.000002f;

struct Tap {
    float direct_delta{}, plane_delta{}, slope{}, tolerance{};
    bool valid{};
};

struct Features {
    std::array<Tap,9> taps{};
    float numeric{}, footprint{}, mismatch{};
    int tap_count{};
};

inline float unwarp_error(float low, float high, int alpha) {
    const float maximum = std::max(std::abs(low),std::abs(high));
    const float qe = gamma(4.0f)*(1.0f+maximum);
    if (alpha == 0) return qe;
    const float relative_error = qe+gamma(1.0f)*maximum;
    const float gain = 1.0f+alpha, denominator = gain-alpha*maximum;
    const float de = gamma(2.0f)*(gain+alpha*maximum)+alpha*relative_error;
    const float quotient = maximum/denominator;
    return (relative_error+quotient*de)/(denominator-de)+gamma(1.0f)*quotient;
}

inline Features sample_features(const ShadowMap& map, const Receiver& r, int radius) {
    const float cell = 1.0f/map.size;
    const auto as_uv = [&](float p) { return warp(p,map.alpha)*0.5f+0.5f; };
    const auto as_ndc = [&](float u) { return unwarp(2.0f*u-1.0f,map.alpha); };
    const auto bounded = [&](float v) { return std::clamp(v,0.5f*cell,1.0f-0.5f*cell); };
    const XY uv{as_uv(r.p.x),as_uv(r.p.y)};
    const int x = std::clamp(static_cast<int>(uv.x/cell),0,map.size-1);
    const int y = std::clamp(static_cast<int>(uv.y/cell),0,map.size-1);
    const XY lo{bounded(uv.x-cell),bounded(uv.y-cell)}, hi{bounded(uv.x+cell),bounded(uv.y+cell)};
    const XY f{std::max(std::abs(as_ndc(lo.x)-r.p.x),std::abs(as_ndc(hi.x)-r.p.x)),
               std::max(std::abs(as_ndc(lo.y)-r.p.y),std::abs(as_ndc(hi.y)-r.p.y))};
    const XY eta{unwarp_error(lo.x*2.0f-1.0f,hi.x*2.0f-1.0f,map.alpha),
                 unwarp_error(lo.y*2.0f-1.0f,hi.y*2.0f-1.0f,map.alpha)};
    const XY magnitude = abs_xy(r.g);
    Features result;
    result.numeric = r.plane_error + dot(magnitude,eta) + 2.5f*depth_step
                   + gamma(5.0f)*(r.z+1.0f+dot(magnitude,f));
    result.footprint = footprint_weight*dot(magnitude+r.error,f*0.5f);
    XY caster;
    if (decode_caster(map.at(x,y).plane,caster)) {
        result.mismatch = footprint_weight*dot(abs_xy(r.g-caster),f*0.5f);
    }
    const auto tap_uncertainty = [&](float p) {
        return gamma(5.0f)*(1.0f+std::abs(p)+(1.0f+std::abs(warp(p,map.alpha)))/warp_derivative(p,map.alpha));
    };
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            Tap& tap = result.taps[result.tap_count++];
            const int tx = x+dx, ty = y+dy;
            if (tx < 0 || tx >= map.size || ty < 0 || ty >= map.size) continue;
            const auto& texel = map.at(tx,ty);
            if (texel.depth >= 1.0f) continue;
            const XY p{as_ndc((tx+0.5f)*cell),as_ndc((ty+0.5f)*cell)};
            const XY offset = p-r.p;
            const float z = r.z+dot(r.g,offset);
            tap.direct_delta = r.z-texel.depth;
            tap.plane_delta = (z > 0.0f && z < 1.0f ? z : r.z)-texel.depth;
            tap.slope = 2.0f*cell*std::max(std::abs(texel.g.x)/warp_derivative(p.x,map.alpha),
                                           std::abs(texel.g.y)/warp_derivative(p.y,map.alpha));
            tap.tolerance = dot(r.error,abs_xy(offset))+dot(magnitude,{tap_uncertainty(p.x),tap_uncertainty(p.y)});
            tap.valid = true;
        }
    }
    return result;
}

inline float global_bias(const Features& f, Method method, float parameter) {
    switch (method) {
        case Method::numeric: return parameter*f.numeric;
        case Method::footprint: return parameter*(f.numeric+f.footprint);
        case Method::full: return parameter*(f.numeric+f.footprint+f.mismatch);
        case Method::selective: {
            // Ablation: only use caster-gradient mismatch when it is small relative
            // to receiver footprint. Strong mismatch may indicate a true boundary.
            const float allowance = f.mismatch <= 0.5f * f.footprint ? f.mismatch : 0.0f;
            return parameter*(f.numeric+f.footprint+allowance);
        }
        case Method::slope: return slope_floor;
        default: return parameter;
    }
}

inline int shadow_count(const Features& f, Method method, float parameter) {
    const float bias = global_bias(f,method,parameter);
    int count = 0;
    for (int i = 0; i < f.tap_count; ++i) {
        const auto& t = f.taps[i];
        if (!t.valid) continue;
        switch (method) {
            case Method::constant: count += t.direct_delta > bias; break;
            case Method::slope: count += t.direct_delta > bias+parameter*t.slope; break;
            case Method::receiver: count += t.plane_delta > bias; break;
            default: count += t.plane_delta > bias+parameter*t.tolerance; break;
        }
    }
    return count;
}

}  // namespace lab
