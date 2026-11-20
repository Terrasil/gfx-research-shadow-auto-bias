#pragma once

#include "shadow.hpp"

#include <ostream>

namespace lab {

struct Counts {
    std::uint64_t lit{}, shadow{}, fp{}, fn{}, contact{}, contact_fn{};

    void add(std::uint8_t category, bool predicted) {
        if (category == 2) { ++lit; fp += predicted; }
        else if (category >= 3) { ++shadow; fn += !predicted; }
        if (category == 4) { ++contact; contact_fn += !predicted; }
    }

    void write(std::ostream& output) const {
        output << ',' << lit << ',' << shadow << ',' << fp << ',' << fn << ',' << contact << ',' << contact_fn;
    }
};

struct GpuInput {
    std::vector<float> positions, gradients, error, texels;
    std::vector<std::uint32_t> planes;
    std::vector<std::uint8_t> base;
};

inline GpuInput pack_inputs(const Frame& frame, const ShadowMap& map) {
    GpuInput data;
    data.positions.reserve(frame.pixels.size()*4);
    data.gradients.reserve(frame.pixels.size()*4);
    data.error.reserve(frame.pixels.size());
    data.base.reserve(frame.pixels.size()*4);
    data.texels.reserve(map.texels.size()*4);
    data.planes.reserve(map.texels.size());
    for (const auto& r : frame.pixels) {
        data.positions.insert(data.positions.end(),{r.p.x,r.p.y,r.z,static_cast<float>(r.category)});
        data.gradients.insert(data.gradients.end(),{r.g.x,r.g.y,r.error.x,r.error.y});
        data.error.push_back(r.plane_error);
        data.base.insert(data.base.end(),r.rgb.begin(),r.rgb.end());
    }
    for (const auto& texel : map.texels) {
        data.texels.insert(data.texels.end(),{texel.depth,texel.g.x,texel.g.y,0.0f});
        data.planes.push_back(texel.plane);
    }
    return data;
}

struct Arm {
    int method = 0;
    float parameter = default_params[0];
};

inline constexpr std::array<const char*, 7> method_labels{
    "Constant depth", "Slope scale", "Receiver plane", "Numerics", "Footprint", "Auto", "Ray reference"
};
inline constexpr std::array<const char*, 4> view_labels{"Shaded", "Reference difference", "Visibility mask", "Bias magnitude"};

}  // namespace lab
