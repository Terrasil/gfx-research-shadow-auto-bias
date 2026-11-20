#include "shadow_pass.hpp"

#include <glad/gl.h>

#include <iterator>

namespace lab {
namespace fs = std::filesystem;
using gfx::research::Shader;
using gfx::research::ShaderSource;
using gfx::research::ShaderStage;

void ShadowPass::load(const fs::path& directory) {
    const auto read = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open shader: " + path.string());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const std::string vertex = read(directory / "fullscreen.vert");
    const std::string comparison = read(directory / "shadow.frag");
    const auto preamble = comparison.find('\n');
    if (preamble == std::string::npos) throw std::runtime_error("Shadow shader is missing its version header.");

    decltype(programs_) next;
    for (std::size_t method = 0; method < next.size(); ++method) {
        for (int diagnostic = 0; diagnostic < 2; ++diagnostic) {
            auto variant = comparison;
            variant.insert(preamble + 1, "#define METHOD " + std::to_string(method)
                + "\n#define DIAGNOSTICS " + std::to_string(diagnostic) + "\n");
            const ShaderSource sources[] = {
                {ShaderStage::Vertex, vertex}, {ShaderStage::Fragment, std::move(variant)}};
            if (!next[method][diagnostic].load(sources)) {
                throw std::runtime_error(std::string("Shadow variant ") + method_labels[method]
                    + ": " + next[method][diagnostic].last_error());
            }
        }
    }
    programs_ = std::move(next);
}

void ShadowPass::bind(const Arm& arm, int radius, bool bias_overlay, int size, int warp,
                      const std::array<unsigned, 5>& inputs) const {
    const Shader& program = programs_[arm.method][bias_overlay ? 1 : 0];
    program.bind();
    constexpr std::array names{"positions", "gradients", "inputError", "depthSlope", "casterPlanes"};
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        glBindTextureUnit(static_cast<unsigned>(i), inputs[i]);
        program.set_int(names[i], static_cast<int>(i));
    }
    program.set_int("mapSize", size);
    program.set_int("radius", radius);
    program.set_float("alpha", static_cast<float>(warp));
    program.set_float("parameter", arm.parameter);
}

}  // namespace lab
