#include "asset_root.hpp"
#include "experiment_output.hpp"
#include "experiment_gpu_timer.hpp"
#include "shadow_renderer.hpp"

#include <gfx/research/application.hpp>
#include <gfx/research/screenshot.hpp>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <chrono>
#include <future>
#include <iostream>
#include <optional>

namespace lab {
namespace fs = std::filesystem;

struct SceneSettings {
    int terrain = 3, sun = 18, azimuth = 37, size = 512, alpha = 8;
    int width = 480, height = 360;
    int yaw = 0, pitch = 0;
    float zoom = 1.0f;
    bool contacts = false, objects = true;
    fs::path custom;
};

struct Scene {
    SceneSettings settings;
    Frame frame;
    ShadowMap map;
};

static Scene build_scene(const fs::path& directory, SceneSettings settings) {
    const fs::path filename = settings.custom.empty()
        ? directory/(std::string(scene_names[settings.terrain])+".pgm") : settings.custom;
    const auto mesh = make_mesh(load_heightmap(filename),settings.objects);
    const Light light(settings.sun,settings.azimuth);
    const auto jobs = static_cast<int>(std::clamp(std::thread::hardware_concurrency(),1u,8u));
    Vec direction{-0.48,-0.81,-0.33};
    if (settings.yaw!=0 || settings.pitch!=0) {
        constexpr double radians=std::numbers::pi/180.0;
        const double yaw=std::atan2(direction.z,direction.x)+settings.yaw*radians;
        const double pitch=std::clamp(std::asin(-normalized(direction).y)+settings.pitch*radians,
            12.0*radians,85.0*radians);
        direction={std::cos(pitch)*std::cos(yaw),-std::sin(pitch),std::cos(pitch)*std::sin(yaw)};
    }
    Frame frame = make_frame(mesh,light,settings.width,settings.height,settings.contacts,jobs,
        1e-6,direction,settings.zoom);
    ShadowMap map(mesh,light,settings.size,settings.alpha,jobs);
    return {std::move(settings),std::move(frame),std::move(map)};
}

class ShadowLab final : public gfx::research::Application {
public:
    ShadowLab(fs::path shaders, fs::path heightmaps, fs::path results, fs::path check)
        : Application({.title="gfx-research-shadow-auto-bias",.width=1440,.height=860,.vsync=true}),
          shaders_(std::move(shaders)), heightmaps_(std::move(heightmaps)), results_(std::move(results)), check_(std::move(check)) {}

    int run_lab() {
        const int status = run();
        return status == 0 ? check_result_ : status;
    }

private:
    bool on_init() override {
        configure_gui();
        renderer_.init(shaders_);
        update_scene(build_scene(heightmaps_,edit_));
        if (!check_.empty()) validate_gl();
        return true;
    }

    void update_scene(Scene scene) {
        renderer_.upload(scene.frame,scene.map);
        current_.emplace(std::move(scene));
        preview_dirty_ = true;
    }

    void request_scene() {
        if (pending_.valid() || timing_.running()) return;
        pending_ = std::async(std::launch::async,build_scene,heightmaps_,edit_);
        message_ = "Preparing terrain and ray reference";
    }

    void on_drop(int count, const char** paths) override {
        if (count < 1 || pending_.valid() || timing_.running()) return;
        const std::string_view filename(paths[0]);
        fs::path path(std::u8string(filename.begin(),filename.end()));
        auto extension = path.extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),
            [](unsigned char ch){return static_cast<char>(std::tolower(ch));});
        if (extension != ".pgm") {
            message_ = "Choose a PGM heightmap.";
            return;
        }
        edit_.custom = std::move(path);
        request_scene();
    }

    Counts counts_for(const std::vector<std::uint8_t>& mask) const {
        Counts counts;
        const auto& pixels = current_->frame.pixels;
        for (std::size_t i=0;i<pixels.size();++i)
            counts.add(pixels[i].category,mask[i]>=128);
        return counts;
    }

    void refresh_previews() {
        for (int side=0;side<2;++side) {
            const auto mask = renderer_.mask(arms_[side],radius_,view_==3);
            scores_[side] = counts_for(mask);
            renderer_.colorize(side,view_);
        }
        preview_dirty_ = false;
    }

    void write_scene(std::ostream& out) const {
        const auto& s = current_->settings;
        const auto name = s.custom.empty() ? scene_names[s.terrain] : "custom";
        out << name << ',' << (s.contacts ? "contact" : "overview") << ','
            << s.sun << ',' << s.azimuth << ',' << s.size << ',' << s.alpha << ','
            << (s.objects ? 1 : 0) << ',' << s.width << ',' << s.height << ',' << s.yaw << ',' << s.pitch
            << ',' << s.zoom << ',' << radius_;
    }

    void save_quality(const fs::path& path) const {
        auto stream = csv_file(path);
        stream << "scene,view,sun_deg,azimuth_deg,map_size,warp,boxes,width,height,camera_yaw_deg,camera_pitch_deg,camera_zoom,pcf_radius,side,method,parameter,"
                 "lit,shadow,false_shadow,missed_shadow,contact,missed_contact\n";
        for (int side=0;side<2;++side) {
            write_scene(stream);
            stream << ',' << (side==0 ? "left" : "right") << ',' << method_names[arms_[side].method]
                << ',' << arms_[side].parameter;
            scores_[side].write(stream);
            stream << '\n';
        }
    }

    void save_timings(const fs::path& path) const {
        auto stream = csv_file(path);
        const auto renderer = gl_csv_string(GL_RENDERER), version = gl_csv_string(GL_VERSION);
        stream << "scene,view,sun_deg,azimuth_deg,map_size,warp,boxes,width,height,camera_yaw_deg,camera_pitch_deg,camera_zoom,pcf_radius,"
                 "pair,order,side,method,parameter,draws,gpu_ms_per_draw,renderer,gl_version\n";
        for (const auto& row : timing_.rows()) {
            const auto& arm = arms_[row.side];
            write_scene(stream);
            stream << ',' << row.pair << ',' << row.order << ',' << (row.side==0 ? "left" : "right")
                << ',' << method_names[arm.method] << ',' << arm.parameter << ',' << timing_.draws()
                << ',' << row.milliseconds << ',' << renderer << ',' << version << '\n';
        }
    }

    void validate_gl() {
        auto out = csv_file(check_/"native-gl-check.csv");
        out << "method,pcf_radius,scored,different_classifications\n";
        for (int radius=0;radius<=1;++radius) {
            for (int m=0;m<static_cast<int>(method_names.size());++m) {
                const auto expected = static_cast<Method>(m);
                const Arm arm{m,default_params[m]};
                const auto gpu = renderer_.mask(arm,radius,false);
                std::size_t compared = 0, different = 0;
                for (std::size_t i=0;i<gpu.size();++i) {
                    const auto& receiver = current_->frame.pixels[i];
                    if (receiver.category<2) continue;
                    const auto f = sample_features(current_->map,receiver,radius);
                    const bool expected_shadow = expected == Method::reference ? receiver.category>=3
                        : shadow_count(f,expected,arm.parameter)>f.tap_count/2;
                    ++compared;
                    different += (gpu[i]>=128)!=expected_shadow;
                }
                out << method_names[m] << ',' << radius << ',' << compared << ',' << different << '\n';
                if (different*500>compared || (expected==Method::reference && different!=0))
                    check_result_=3;
            }
        }
        if (glGetError()!=GL_NO_ERROR) check_result_=4;
    }

    void on_frame(const gfx::research::FrameInfo& frame) override {
        if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try {
                update_scene(pending_.get());
                message_ = "Scene ready.";
            } catch (const std::exception& error) { message_ = error.what(); }
        }
        if (preview_dirty_) refresh_previews();
        if (timing_.step(renderer_,arms_,radius_)) {
            try {
                const auto path = check_.empty() ? output_path(results_ / "timing","gpu",".csv") : check_/"gpu-timing-smoke.csv";
                write_timings_if_valid(path);
                message_ = "Saved "+path.string();
                if (!check_.empty()) { close(); return; }
            } catch (const std::exception& error) {
                message_ = error.what();
                if (!check_.empty()) { check_result_=4; close(); return; }
            }
        }
        gfx::research::Framebuffer::bind_default();
        if (!check_.empty() && frame.frame_index==3) {
            GLint previous = GL_BACK;
            glGetIntegerv(GL_READ_BUFFER,&previous);
            glReadBuffer(GL_FRONT);
            if (!gfx::research::save_framebuffer_png(check_/"control-panel.png",frame.width,frame.height))
                check_result_ = 4;
            glReadBuffer(previous);
            renderer_.save_comparison(check_/"comparison.png");
            save_quality(check_/"quality.csv");
            timing_.start(2,2);
        }
        glViewport(0,0,frame.width,frame.height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.025f,0.035f,0.047f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void write_timings_if_valid(const fs::path& path) const {
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL reported an error during timing.");
        save_timings(path);
    }

#include "shadow_gui.inl"

    fs::path shaders_, heightmaps_, results_, check_;
    SceneSettings edit_;
    std::optional<Scene> current_;
    std::future<Scene> pending_;
    ShadowRenderer renderer_;
    ExperimentGpuTimer timing_;
    std::array<Arm,2> arms_{{{0,default_params[0]},{5,default_params[5]}}};
    std::array<Counts,2> scores_{};
    int radius_=0, view_=0, check_result_=0;
    bool preview_dirty_=true;
    std::string message_;
};

}  // namespace lab

int reference_viewer_main(int argc, char** argv) {
    try {
        namespace fs = std::filesystem;
        const std::string_view command=argc>1 ? std::string_view(argv[1]) : "";
        const bool check = command=="--gl-check" || command=="--headless-gl-check";
        if (check && argc!=3) throw std::runtime_error("Supply an output directory for the OpenGL check.");
        const auto shaders = lab::asset_root(argv[0],"shaders/shadow.frag")/"shaders";
        const auto maps = lab::asset_root(argv[0],"heightmaps/ramp.pgm")/"heightmaps";
        return lab::ShadowLab(shaders, maps, lab::default_results_directory(argv[0]),
            check ? fs::path(argv[2]) : fs::path{}).run_lab();
    } catch (const std::exception& error) {
        std::cerr << "gfx-research-shadow-auto-bias: " << error.what() << '\n';
        return 1;
    }
}
