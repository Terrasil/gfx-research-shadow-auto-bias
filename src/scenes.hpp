#pragma once

#include "geometry.hpp"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lab {

inline constexpr double terrain_half = 112.0;
inline constexpr double terrain_min = -16.0;
inline constexpr double terrain_step = 1.0 / 1024.0;
inline constexpr int terrain_samples = 129;
inline constexpr std::array scene_names{"ramp", "hills", "terraces", "ridge"};

struct Heightmap {
    int width{}, height{};
    std::vector<double> data;

    Vec vertex(int x, int z) const {
        return {terrain_half*(2.0*x/(width-1)-1.0), data[z*width+x],
                terrain_half*(2.0*z/(height-1)-1.0)};
    }

    double at(double x, double z) const {
        const double fx = (x/terrain_half+1.0)*0.5*(width-1);
        const double fz = (z/terrain_half+1.0)*0.5*(height-1);
        const int i = std::clamp(static_cast<int>(fx), 0, width-2);
        const int j = std::clamp(static_cast<int>(fz), 0, height-2);
        const double u = fx-i, v = fz-j;
        const double a = data[j*width+i], b = data[j*width+i+1];
        const double c = data[(j+1)*width+i], d = data[(j+1)*width+i+1];
        if (u+v <= 1.0) return a+(b-a)*u+(c-a)*v;
        return d+(c-d)*(1.0-u)+(b-d)*(1.0-v);
    }
};

inline void write_heightmaps(const std::filesystem::path& path) {
    std::filesystem::create_directories(path);
    for (std::string_view scene : scene_names) {
        std::ofstream pgm(path/(std::string(scene)+".pgm"), std::ios::binary);
        pgm.exceptions(std::ios::badbit | std::ios::failbit);
        pgm << "P5\n# World height in metres = -16 + sample / 1024\n"
            << terrain_samples << ' ' << terrain_samples << "\n65535\n";
        for (int j = 0; j < terrain_samples; ++j) {
            for (int i = 0; i < terrain_samples; ++i) {
                const double x = 2.0*i/(terrain_samples-1)-1.0;
                const double z = 2.0*j/(terrain_samples-1)-1.0;
                double h = 3.0 + (74*(i-64)+35*(j-64))*terrain_step;
                if (scene == "hills") {
                    h = 6.7 + 5.0*std::sin(4.8*x+1.3*z) + 4.0*std::cos(5.6*z-2.5*x)
                            + 1.4*std::sin(14.0*x+9.0*z);
                } else if (scene == "terraces") {
                    const double curve = 5.2*z+1.7*x+0.7*std::sin(4.0*x);
                    h = 8.0 + 2.8*std::floor(curve) + 1.4*std::sin(9.0*x+4.0*z);
                } else if (scene == "ridge") {
                    const double ridge = z+0.29*x-0.16*std::sin(4.8*x);
                    h = 2.0 + 29.0*std::exp(-5.8*std::abs(ridge))*(0.74+0.26*std::cos(6.0*x))
                            + 7.3*std::sin(5.5*x-1.3*z)*std::sin(4.3*z+1.0)
                            + 1.0*std::sin(28.0*x+14.0*z)*std::cos(18.0*z-11.0*x);
                }
                const int sample = static_cast<int>(std::clamp(std::lround((h-terrain_min)/terrain_step), 0L, 65535L));
                pgm.put(static_cast<char>(sample >> 8));
                pgm.put(static_cast<char>(sample & 255));
            }
        }
    }
}

inline std::string pgm_token(std::istream& stream) {
    std::string token;
    for (int c; (c = stream.peek()) != EOF;) {
        if (c == '#') stream.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        else if (std::isspace(c)) stream.get();
        else break;
    }
    for (int c; (c = stream.peek()) != EOF && !std::isspace(c) && c != '#';) {
        token += static_cast<char>(stream.get());
    }
    if (token.empty()) throw std::runtime_error("Incomplete PGM header or data.");
    return token;
}

inline Heightmap load_heightmap(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open heightmap: "+path.string());
    const std::string magic = pgm_token(stream);
    if (magic != "P5" && magic != "P2") throw std::runtime_error("PGM P5 or P2 expected.");
    const int width = std::stoi(pgm_token(stream)), height = std::stoi(pgm_token(stream));
    const int peak = std::stoi(pgm_token(stream));
    if (width < 2 || height < 2 || width > 1024 || height > 1024 || peak < 1 || peak > 65535)
        throw std::runtime_error("PGM requires 2 to 1024 samples per axis and a max value of 1 to 65535.");
    if (magic == "P5") {
        const int delimiter = stream.get();
        if (delimiter == EOF || !std::isspace(delimiter)) throw std::runtime_error("Missing PGM raster delimiter.");
        if (delimiter == '\r' && stream.peek() == '\n') stream.get();
    }
    Heightmap map{width, height, std::vector<double>(width*height)};
    for (double& h : map.data) {
        int sample;
        if (magic == "P2") {
            sample = std::stoi(pgm_token(stream));
        } else {
            sample = stream.get();
            if (peak > 255 && sample != EOF) {
                const int low = stream.get();
                sample = low != EOF ? ((sample << 8) | low) : EOF;
            }
        }
        if (sample < 0 || sample > peak) throw std::runtime_error("Invalid or truncated PGM raster.");
        h = terrain_min+terrain_step*(65535.0*sample/peak);
    }
    return map;
}

struct Box {
    double x, z, sx, sz, rise;

    double horizontal_distance(Vec p) const {
        return std::hypot(std::max(0.0, std::abs(p.x-x)-sx*0.5),
                          std::max(0.0, std::abs(p.z-z)-sz*0.5));
    }
};

inline constexpr std::array<Box, 13> boxes{{
    {-10,-11,5.0,7.0,7.0}, {16,2,2.5,6.0,1.2}, {12,21,7.0,4.0,3.3},
    {-28,21,3.2,5.5,1.2}, {24,-35,6.0,3.0,4.4}, {60,36,4.0,5.0,3.4},
    {-67,63,6.0,5.0,7.0}, {-77,77,4.2,8.0,1.5}, {-89,78,7.0,6.0,4.5},
    {-66,87,4.8,3.0,3.5}, {-91,60,3.0,6.0,2.0},
    {82,-69,6.0,8.0,5.0}, {79,-52,2.8,5.0,1.1}
}};

inline std::vector<Triangle> make_triangles(const Heightmap& map, bool with_boxes) {
    std::vector<Triangle> triangles;
    triangles.reserve(2*(map.width-1)*(map.height-1)+12*boxes.size());
    for (int z = 0; z+1 < map.height; ++z) {
        for (int x = 0; x+1 < map.width; ++x) {
            const Vec a = map.vertex(x,z), b = map.vertex(x+1,z);
            const Vec c = map.vertex(x,z+1), d = map.vertex(x+1,z+1);
            triangles.emplace_back(a,c,b,0);
            triangles.emplace_back(b,c,d,0);
        }
    }
    if (!with_boxes) return triangles;
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        const auto& box = boxes[i];
        const double x0 = box.x-box.sx*0.5, x1 = box.x+box.sx*0.5;
        const double z0 = box.z-box.sz*0.5, z1 = box.z+box.sz*0.5;
        const double low = terrain_min-4.0;
        double high = -1e9;
        for (int z = 0; z <= 4; ++z) for (int x = 0; x <= 4; ++x)
            high = std::max(high, map.at(std::lerp(x0,x1,x/4.0), std::lerp(z0,z1,z/4.0)));
        high += box.rise;
        const Vec a{x0,low,z0}, b{x1,low,z0}, c{x0,low,z1}, d{x1,low,z1};
        const Vec e{x0,high,z0}, f{x1,high,z0}, g{x0,high,z1}, h{x1,high,z1};
        const int id = static_cast<int>(i)+1;
        auto quad = [&](Vec p, Vec q, Vec r, Vec s) {
            triangles.emplace_back(p,q,r,id);
            triangles.emplace_back(p,r,s,id);
        };
        quad(e,g,h,f); quad(a,b,d,c); quad(a,e,f,b);
        quad(c,d,h,g); quad(a,c,g,e); quad(b,f,h,d);
    }
    return triangles;
}

inline Bvh make_mesh(const Heightmap& map, bool with_boxes) {
    return Bvh(make_triangles(map, with_boxes));
}

}  // namespace lab
