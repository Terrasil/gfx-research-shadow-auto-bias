#pragma once

#include "shadow_pass.hpp"

#include <gfx/research/framebuffer.hpp>

namespace lab {

struct LightSettings {
    int elevation = 18;
    int azimuth = 37;
    int resolution = 512;
    int warp = 8;

    bool operator==(const LightSettings&) const = default;
};

enum class LiveView { shaded, visibility, bias, light_depth };
enum class MeshSlot { scene, study };

class LiveRenderer {
public:
    LiveRenderer() = default;
    LiveRenderer(const LiveRenderer&) = delete;
    LiveRenderer& operator=(const LiveRenderer&) = delete;
    ~LiveRenderer();

    void load_shaders(const std::filesystem::path& directory);
    void upload(std::span<const Triangle> triangles, MeshSlot slot = MeshSlot::scene);
    void use_mesh(MeshSlot slot);
    void prepare(int width, int height, const glm::mat4& view_projection, const LightSettings& settings);
    void bind_mask(const Arm& arm, int radius, bool bias_overlay) const;
    void draw() const { pass_.draw(); }
    void render_mask(const std::array<Arm, 2>& arms, int radius, LiveView view, int split_x) const;
    void display(const glm::mat4& view_projection, const glm::vec3& eye, LiveView view, int split_x) const;

    int triangle_count() const { return meshes_[static_cast<int>(active_mesh_)].vertices / 3; }
    unsigned map_revision() const { return map_revision_; }
    std::vector<std::uint8_t> read_mask() const;
    std::vector<float> read_depth() const;
    std::vector<glm::vec4> read_receivers() const;

private:
    void update_map();
    void resize(int width, int height);
    void draw_mesh() const;
    void bind_mesh();

    struct Mesh {
        unsigned buffer = 0;
        int vertices = 0;
    };

    gfx::research::Shader scene_shader_, light_shader_, display_shader_;
    gfx::research::Framebuffer scene_, light_map_, mask_;
    ShadowPass pass_;
    LightSettings settings_;
    Light light_{settings_.elevation, settings_.azimuth};
    std::array<Mesh, 2> meshes_;
    MeshSlot active_mesh_ = MeshSlot::scene;
    unsigned vao_ = 0, map_revision_ = 0;
    bool map_dirty_ = true;
};

}  // namespace lab
