#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <thread>
#include <vector>

namespace lab {

struct Vec {
    double x{}, y{}, z{};
    double operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    Vec operator+(Vec b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec operator-(Vec b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec operator*(double s) const { return {x * s, y * s, z * s}; }
};

inline double dot(Vec a, Vec b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec cross(Vec a, Vec b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline Vec normalized(Vec v) { return v * (1.0 / std::sqrt(dot(v, v))); }

struct Bounds {
    Vec lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};

    void add(Vec p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    void add(const Bounds& b) { add(b.lo); add(b.hi); }
};

struct Triangle {
    Vec a, ab, ac, normal, center;
    Bounds bounds;
    int object{};

    Triangle(Vec p, Vec q, Vec r, int id)
        : a(p), ab(q-p), ac(r-p), normal(normalized(cross(ab, ac))),
          center((p+q+r)*(1.0/3.0)), object(id) {
        bounds.add(p);
        bounds.add(q);
        bounds.add(r);
    }
};

struct Ray {
    Vec origin, direction, inverse;

    Ray(Vec p, Vec d) : origin(p), direction(d) {
        for (double* value : {&d.x, &d.y, &d.z}) {
            *value = std::abs(*value) > 1e-14 ? 1.0 / *value : 0.0;
        }
        inverse = d;
    }
};

struct Hit {
    double distance;
    int triangle = -1;
};

class Bvh {
    struct Node {
        Bounds bounds;
        int left = -1, right = -1, begin = 0, count = 0;
    };

    std::vector<Node> nodes_;
    std::vector<int> order_;

    int build(int begin, int end) {
        const int index = static_cast<int>(nodes_.size());
        nodes_.emplace_back();
        Bounds centers;
        for (int i = begin; i < end; ++i) {
            const auto& t = triangles[order_[i]];
            nodes_[index].bounds.add(t.bounds);
            centers.add(t.center);
        }
        if (end - begin <= 6) {
            nodes_[index].begin = begin;
            nodes_[index].count = end - begin;
            return index;
        }
        const Vec span = centers.hi - centers.lo;
        const int axis = span.x > span.y ? (span.x > span.z ? 0 : 2) : (span.y > span.z ? 1 : 2);
        const int mid = (begin + end) / 2;
        std::nth_element(order_.begin()+begin, order_.begin()+mid, order_.begin()+end,
            [&](int a, int b) {
                const double ca = triangles[a].center[axis], cb = triangles[b].center[axis];
                return ca != cb ? ca < cb : a < b;
            });
        const int left = build(begin, mid), right = build(mid, end);
        nodes_[index].left = left;
        nodes_[index].right = right;
        return index;
    }

    static double entry(const Bounds& bounds, const Ray& ray, double minimum, double maximum) {
        constexpr double padding = 1e-8;
        for (int axis = 0; axis < 3; ++axis) {
            const double lo = bounds.lo[axis]-padding-ray.origin[axis];
            const double hi = bounds.hi[axis]+padding-ray.origin[axis];
            const double reciprocal = ray.inverse[axis];
            if (reciprocal == 0.0) {
                if (lo > 0.0 || hi < 0.0) return 1e30;
                continue;
            }
            const double a = lo * reciprocal, b = hi * reciprocal;
            minimum = std::max(minimum, std::min(a, b));
            maximum = std::min(maximum, std::max(a, b));
            if (minimum > maximum) return 1e30;
        }
        return minimum;
    }

public:
    std::vector<Triangle> triangles;

    explicit Bvh(std::vector<Triangle> geometry) : triangles(std::move(geometry)) {
        order_.resize(triangles.size());
        std::iota(order_.begin(), order_.end(), 0);
        build(0, static_cast<int>(order_.size()));
    }

    Hit intersect(const Ray& ray, double minimum, double maximum, int skip = -1) const {
        Hit hit{maximum};
        struct Pending { int node; double entry; };
        std::array<Pending, 64> stack{};
        int n = 0;
        if (const double root = entry(nodes_[0].bounds, ray, minimum, maximum); root < maximum) {
            stack[n++] = {0, root};
        }
        while (n > 0) {
            const auto next = stack[--n];
            if (next.entry > hit.distance) continue;
            const auto& node = nodes_[next.node];
            if (node.count == 0) {
                const double a = entry(nodes_[node.left].bounds, ray, minimum, hit.distance);
                const double b = entry(nodes_[node.right].bounds, ray, minimum, hit.distance);
                if (a < b) {
                    if (b < hit.distance) stack[n++] = {node.right, b};
                    if (a < hit.distance) stack[n++] = {node.left, a};
                } else {
                    if (a < hit.distance) stack[n++] = {node.left, a};
                    if (b < hit.distance) stack[n++] = {node.right, b};
                }
                continue;
            }
            for (int i = node.begin; i < node.begin+node.count; ++i) {
                const int index = order_[i];
                if (index == skip) continue;
                const auto& t = triangles[index];
                const Vec p = cross(ray.direction, t.ac);
                const double determinant = dot(t.ab, p);
                if (std::abs(determinant) < 1e-12) continue;
                const double reciprocal = 1.0 / determinant;
                const Vec rel = ray.origin-t.a;
                const double u = dot(rel, p)*reciprocal;
                if (u < -1e-10 || u > 1.0+1e-10) continue;
                const Vec q = cross(rel, t.ab);
                const double v = dot(ray.direction, q)*reciprocal;
                if (v < -1e-10 || u+v > 1.0+1e-10) continue;
                const double d = dot(t.ac, q)*reciprocal;
                if (d > minimum && (d < hit.distance || (d == hit.distance && index < hit.triangle))) {
                    hit = {d, index};
                }
            }
        }
        return hit;
    }
};

template<typename Function>
void parallel_rows(int rows, int jobs, Function callback) {
    std::atomic<int> next{0};
    auto work = [&] {
        for (int row = next.fetch_add(1); row < rows; row = next.fetch_add(1)) callback(row);
    };
    std::vector<std::jthread> threads;
    for (int i = 1; i < jobs; ++i) threads.emplace_back(work);
    work();
}

}  // namespace lab
