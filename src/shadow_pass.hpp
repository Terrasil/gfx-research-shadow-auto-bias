#pragma once

#include "experiment_data.hpp"

#include <gfx/research/fullscreen_triangle.hpp>
#include <gfx/research/shader.hpp>

namespace lab {

class ShadowPass {
public:
    void load(const std::filesystem::path& directory);
    void bind(const Arm& arm, int radius, bool bias_overlay, int size, int warp,
              const std::array<unsigned, 5>& inputs) const;
    void draw() const { triangle_.draw(); }

private:
    std::array<std::array<gfx::research::Shader, 2>, method_names.size()> programs_;
    gfx::research::FullscreenTriangle triangle_;
};

}  // namespace lab
