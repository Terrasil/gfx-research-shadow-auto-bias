#include "live_renderer.hpp"

#include <glad/gl.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstddef>

namespace lab {
namespace {
using gfx::research::ColorAttachmentDesc;
using gfx::research::Framebuffer;
using gfx::research::Shader;
using gfx::research::ShaderFile;
using gfx::research::ShaderStage;

constexpr ColorAttachmentDesc attachment(unsigned format) {
    return {format, GL_NEAREST, GL_NEAREST};
}

glm::vec3 to_glm(Vec v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

struct Vertex {
    float position[3];
    float normal[3];
    float terrain;
};

void set_light(const Shader& shader, const Light& light) {
    shader.set_vec3("lightU", to_glm(light.u));
    shader.set_vec3("lightV", to_glm(light.v));
    shader.set_vec3("sunDirection", to_glm(light.to_sun));
    shader.set_float("lightHalf", light_half);
    shader.set_float("depthRange", depth_range);
}

void set_texture(const Shader& shader, int unit, const char* name, unsigned texture) {
    glBindTextureUnit(unit, texture);
    shader.set_int(name, unit);
}

template<class Pixel>
std::vector<Pixel> read_texture(unsigned texture, int width, int height, GLenum format, GLenum type) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width) * height);
    GLint alignment = 0;
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTextureImage(texture, 0, format, type, static_cast<GLsizei>(pixels.size() * sizeof(Pixel)), pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    return pixels;
}

}  // namespace

LiveRenderer::~LiveRenderer() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    for (auto& mesh : meshes_)
        if (mesh.buffer) glDeleteBuffers(1, &mesh.buffer);
}

void LiveRenderer::load_shaders(const std::filesystem::path& directory) {
    const ShaderFile scene_files[] = {
        {ShaderStage::Vertex, directory / "scene.vert"},
        {ShaderStage::Fragment, directory / "scene.frag"}};
    const ShaderFile light_files[] = {
        {ShaderStage::Vertex, directory / "light.vert"},
        {ShaderStage::Geometry, directory / "light.geom"},
        {ShaderStage::Fragment, directory / "light.frag"}};
    const ShaderFile display_files[] = {
        {ShaderStage::Vertex, directory / "fullscreen.vert"},
        {ShaderStage::Fragment, directory / "live_shade.frag"}};

    Shader scene, light, display;
    if (!scene.load(scene_files)) throw std::runtime_error("Scene shader: " + scene.last_error());
    if (!light.load(light_files)) throw std::runtime_error("Light shader: " + light.last_error());
    if (!display.load(display_files)) throw std::runtime_error("Display shader: " + display.last_error());
    pass_.load(directory);
    scene_shader_ = std::move(scene);
    light_shader_ = std::move(light);
    display_shader_ = std::move(display);
    map_dirty_ = true;
}

void LiveRenderer::upload(std::span<const Triangle> triangles, MeshSlot slot) {
    std::vector<Vertex> vertices;
    vertices.reserve(triangles.size() * 3);
    for (const auto& tri : triangles) {
        const auto normal = to_glm(tri.normal);
        for (const Vec point : {tri.a, tri.a + tri.ab, tri.a + tri.ac}) {
            const auto p = to_glm(point);
            vertices.push_back({{p.x, p.y, p.z}, {normal.x, normal.y, normal.z}, tri.object == 0 ? 1.0f : 0.0f});
        }
    }
    unsigned next_buffer = 0;
    glCreateBuffers(1, &next_buffer);
    glNamedBufferStorage(next_buffer, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);
    if (const auto error = glGetError(); error != GL_NO_ERROR) {
        glDeleteBuffers(1, &next_buffer);
        throw std::runtime_error("OpenGL failed to upload the terrain mesh (" + std::to_string(error) + ").");
    }
    if (!vao_) {
        glCreateVertexArrays(1, &vao_);
        constexpr std::array offsets{offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, terrain)};
        for (unsigned i = 0; i < offsets.size(); ++i) {
            glEnableVertexArrayAttrib(vao_, i);
            glVertexArrayAttribFormat(vao_, i, i == 2 ? 1 : 3, GL_FLOAT, GL_FALSE, static_cast<unsigned>(offsets[i]));
            glVertexArrayAttribBinding(vao_, i, 0);
        }
    }
    auto& mesh = meshes_[static_cast<int>(slot)];
    if (mesh.buffer) glDeleteBuffers(1, &mesh.buffer);
    mesh = {next_buffer, static_cast<int>(vertices.size())};
    if (slot == active_mesh_) bind_mesh();
}

void LiveRenderer::use_mesh(MeshSlot slot) {
    if (slot == active_mesh_) return;
    active_mesh_ = slot;
    bind_mesh();
}

void LiveRenderer::bind_mesh() {
    const auto& mesh = meshes_[static_cast<int>(active_mesh_)];
    glVertexArrayVertexBuffer(vao_, 0, mesh.buffer, 0, sizeof(Vertex));
    map_dirty_ = true;
}

void LiveRenderer::draw_mesh() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, meshes_[static_cast<int>(active_mesh_)].vertices);
}

void LiveRenderer::resize(int width, int height) {
    if (scene_.width() == width && scene_.height() == height) return;

    constexpr ColorAttachmentDesc surfaces[] = {
        attachment(GL_RGBA32F), attachment(GL_RGBA32F), attachment(GL_R32F), attachment(GL_RGBA8)};
    constexpr ColorAttachmentDesc visibility[]{attachment(GL_RG8)};
    Framebuffer scene, mask;
    if (!scene.create(width, height, surfaces) || !mask.create(width, height, visibility, false))
        throw std::runtime_error("Cannot allocate the camera targets.");
    scene_ = std::move(scene);
    mask_ = std::move(mask);
}

void LiveRenderer::update_map() {
    const int size = settings_.resolution;
    if (light_map_.width() != size) {
        constexpr ColorAttachmentDesc texels[]{attachment(GL_RGBA32F), attachment(GL_R32UI)};
        Framebuffer map;
        if (!map.create(size, size, texels)) throw std::runtime_error("Cannot allocate the shadow map.");
        light_map_ = std::move(map);
    }

    light_map_.bind();
    glViewport(0, 0, size, size);
    const float empty[]{1.0f, 0.0f, 0.0f, 0.0f};
    const unsigned empty_plane[]{no_plane, 0, 0, 0};
    glClearBufferfv(GL_COLOR, 0, empty);
    glClearBufferuiv(GL_COLOR, 1, empty_plane);
    glClearBufferfv(GL_DEPTH, 0, empty);
    light_shader_.bind();
    set_light(light_shader_, light_);
    light_shader_.set_int("mapSize", size);
    light_shader_.set_float("alpha", static_cast<float>(settings_.warp));
    draw_mesh();
    map_dirty_ = false;
    ++map_revision_;
}

void LiveRenderer::prepare(int width, int height, const glm::mat4& view_projection, const LightSettings& settings) {
    resize(width, height);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    if (settings_ != settings) {
        settings_ = settings;
        light_ = Light(settings_.elevation, settings_.azimuth);
        map_dirty_ = true;
    }
    if (map_dirty_) update_map();

    scene_.bind();
    glViewport(0, 0, width, height);
    const float zero[]{0, 0, 0, 0};
    const float far_depth = 1.0f;
    for (int i = 0; i < 4; ++i) glClearBufferfv(GL_COLOR, i, zero);
    glClearBufferfv(GL_DEPTH, 0, &far_depth);
    scene_shader_.bind();
    scene_shader_.set_mat4("viewProjection", view_projection);
    set_light(scene_shader_, light_);
    draw_mesh();
}

void LiveRenderer::bind_mask(const Arm& arm, int radius, bool bias_overlay) const {
    mask_.bind();
    glViewport(0, 0, mask_.width(), mask_.height());
    glDisable(GL_DEPTH_TEST);
    pass_.bind(arm, radius, bias_overlay, settings_.resolution, settings_.warp,
        {scene_.color(0), scene_.color(1), scene_.color(2), light_map_.color(0), light_map_.color(1)});
}

void LiveRenderer::render_mask(const std::array<Arm, 2>& arms, int radius, LiveView view, int split_x) const {
    if (view == LiveView::light_depth) return;
    const bool bias = view == LiveView::bias;
    if (split_x < 0) {
        bind_mask(arms[1], radius, bias);
        draw();
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, split_x, mask_.height());
    bind_mask(arms[0], radius, bias);
    draw();
    glScissor(split_x, 0, mask_.width() - split_x, mask_.height());
    bind_mask(arms[1], radius, bias);
    draw();
    glDisable(GL_SCISSOR_TEST);
}

void LiveRenderer::display(const glm::mat4& view_projection, const glm::vec3& eye,
                           LiveView view, int split_x) const {
    Framebuffer::bind_default();
    glViewport(0, 0, scene_.width(), scene_.height());
    glDisable(GL_DEPTH_TEST);
    display_shader_.bind();
    display_shader_.set_mat4("inverseViewProjection", glm::inverse(view_projection));
    display_shader_.set_vec3("eye", eye);
    display_shader_.set_vec3("sunDirection", to_glm(light_.to_sun));
    display_shader_.set_vec2("resolution", {static_cast<float>(scene_.width()), static_cast<float>(scene_.height())});
    display_shader_.set_int("displayMode", static_cast<int>(view));
    display_shader_.set_int("splitX", split_x);
    set_texture(display_shader_, 0, "positions", scene_.color(0));
    set_texture(display_shader_, 1, "surface", scene_.color(3));
    set_texture(display_shader_, 2, "visibility", mask_.color());
    set_texture(display_shader_, 3, "depthSlope", light_map_.color(0));
    draw();
}

std::vector<std::uint8_t> LiveRenderer::read_mask() const {
    return read_texture<std::uint8_t>(mask_.color(), mask_.width(), mask_.height(), GL_RED, GL_UNSIGNED_BYTE);
}

std::vector<float> LiveRenderer::read_depth() const {
    return read_texture<float>(light_map_.color(), light_map_.width(), light_map_.height(), GL_RED, GL_FLOAT);
}

std::vector<glm::vec4> LiveRenderer::read_receivers() const {
    return read_texture<glm::vec4>(scene_.color(0), scene_.width(), scene_.height(), GL_RGBA, GL_FLOAT);
}

}  // namespace lab
