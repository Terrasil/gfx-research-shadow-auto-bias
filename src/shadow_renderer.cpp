#include "shadow_renderer.hpp"

#include <gfx/research/screenshot.hpp>
#include <glad/gl.h>

namespace lab {
namespace fs = std::filesystem;
using gfx::research::ShaderStage;
using gfx::research::Framebuffer;

ShadowRenderer::~ShadowRenderer() {
    if (input_[0]) glDeleteTextures(static_cast<int>(input_.size()), input_.data());
}

void ShadowRenderer::init(const fs::path& directory) {
    pass_.load(directory);
    const gfx::research::ShaderFile shading[] = {
        {ShaderStage::Vertex, directory / "fullscreen.vert"},
        {ShaderStage::Fragment, directory / "shade.frag"}};
    if (!shade_.load(shading)) throw std::runtime_error("Display shader: " + shade_.last_error());
}

void ShadowRenderer::upload(const Frame& frame, const ShadowMap& map) {
    const auto data = pack_inputs(frame,map);
    Framebuffer next_mask;
    std::array<Framebuffer,2> next_previews;
    constexpr gfx::research::ColorAttachmentDesc color[]{ {GL_RGBA8,GL_NEAREST,GL_NEAREST} };
    if (!next_mask.create(frame.width,frame.height,color,false)) throw std::runtime_error("Cannot allocate shadow mask.");
    for (auto& buffer : next_previews)
        if (!buffer.create(frame.width,frame.height,color,false)) throw std::runtime_error("Cannot allocate preview texture.");

    std::array<GLuint,6> next_input{};
    glCreateTextures(GL_TEXTURE_2D,static_cast<int>(next_input.size()),next_input.data());
    const auto upload_texture = [](GLuint id, GLenum storage, GLenum format, GLenum type,
                                   int width, int height, const void* pixels) {
        glTextureStorage2D(id,1,storage,width,height);
        glTextureParameteri(id,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTextureParameteri(id,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTextureParameteri(id,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTextureParameteri(id,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTextureSubImage2D(id,0,0,0,width,height,format,type,pixels);
    };
    upload_texture(next_input[0],GL_RGBA32F,GL_RGBA,GL_FLOAT,frame.width,frame.height,data.positions.data());
    upload_texture(next_input[1],GL_RGBA32F,GL_RGBA,GL_FLOAT,frame.width,frame.height,data.gradients.data());
    upload_texture(next_input[2],GL_R32F,GL_RED,GL_FLOAT,frame.width,frame.height,data.error.data());
    upload_texture(next_input[3],GL_RGBA32F,GL_RGBA,GL_FLOAT,map.size,map.size,data.texels.data());
    upload_texture(next_input[4],GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,map.size,map.size,data.planes.data());
    upload_texture(next_input[5],GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,frame.width,frame.height,data.base.data());
    if (const GLenum error=glGetError(); error!=GL_NO_ERROR) {
        glDeleteTextures(static_cast<int>(next_input.size()),next_input.data());
        throw std::runtime_error("OpenGL failed to upload the test scene (error "+std::to_string(error)+").");
    }
    glDeleteTextures(static_cast<int>(input_.size()),input_.data());
    input_ = next_input;
    mask_ = std::move(next_mask);
    previews_ = std::move(next_previews);
    width_ = frame.width;
    height_ = frame.height;
    size_ = map.size;
    alpha_ = map.alpha;
}

void ShadowRenderer::bind_mask(const Arm& arm, int radius, bool show_bias) {
    mask_.bind();
    glViewport(0,0,width_,height_);
    glDisable(GL_DEPTH_TEST);
    pass_.bind(arm, radius, show_bias, size_, alpha_,
        {input_[0], input_[1], input_[2], input_[3], input_[4]});
}

void ShadowRenderer::draw() const { pass_.draw(); }

std::vector<std::uint8_t> ShadowRenderer::mask(const Arm& arm, int radius, bool show_bias) {
    bind_mask(arm,radius,show_bias);
    draw();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_)*height_);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glGetTextureImage(mask_.color(),0,GL_RED,GL_UNSIGNED_BYTE,static_cast<int>(pixels.size()),pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT,4);
    return pixels;
}

void ShadowRenderer::colorize(int side, int view) {
    previews_[side].bind();
    glViewport(0,0,width_,height_);
    shade_.bind();
    glBindTextureUnit(0,mask_.color());
    glBindTextureUnit(1,input_[0]);
    glBindTextureUnit(2,input_[5]);
    shade_.set_int("mask",0);
    shade_.set_int("positions",1);
    shade_.set_int("base",2);
    shade_.set_int("view",view);
    draw();
}

unsigned ShadowRenderer::preview(int side) const { return previews_[side].color(); }

void ShadowRenderer::save_comparison(const fs::path& path) {
    Framebuffer image;
    constexpr gfx::research::ColorAttachmentDesc color[]{ {GL_RGBA8,GL_NEAREST,GL_NEAREST} };
    if (!image.create(width_*2,height_,color,false)) throw std::runtime_error("Cannot allocate capture target.");
    for (int i=0;i<2;++i) {
        glBlitNamedFramebuffer(previews_[i].id(),image.id(),0,0,width_,height_,
            width_*i,0,width_*(i+1),height_,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    image.bind();
    const bool saved=gfx::research::save_framebuffer_png(path,width_*2,height_);
    Framebuffer::bind_default();
    if (!saved) throw std::runtime_error("Cannot write "+path.string());
}

}  // namespace lab
