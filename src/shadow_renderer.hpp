#pragma once

#include "shadow_pass.hpp"

#include <gfx/research/framebuffer.hpp>
#include <gfx/research/shader.hpp>

namespace lab {

class ShadowRenderer {
public:
    ~ShadowRenderer();
    void init(const std::filesystem::path& directory);
    void upload(const Frame& frame, const ShadowMap& map);

    std::vector<std::uint8_t> mask(const Arm& arm, int radius, bool show_bias);
    void colorize(int side, int view);
    void bind_mask(const Arm& arm, int radius, bool show_bias);
    void draw() const;
    unsigned preview(int side) const;
    void save_comparison(const std::filesystem::path& path);

private:
    std::array<unsigned,6> input_{};
    ShadowPass pass_;
    gfx::research::Shader shade_;
    gfx::research::Framebuffer mask_;
    std::array<gfx::research::Framebuffer,2> previews_;
    int width_ = 0, height_ = 0, size_ = 0, alpha_ = 0;
};

}  // namespace lab
