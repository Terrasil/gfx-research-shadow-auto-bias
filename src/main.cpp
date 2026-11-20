#include "asset_root.hpp"
#include "benchmark.hpp"
#include "experiment_gpu_timer.hpp"
#include "experiment_output.hpp"
#include "live_renderer.hpp"
#include "publication_suite.hpp"

#include <gfx/research/application.hpp>
#include <gfx/research/orbit_camera.hpp>
#include <gfx/research/screenshot.hpp>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/geometric.hpp>

#include <functional>
#include <iostream>
#include <numeric>

int reference_viewer_main(int argc, char** argv);

namespace lab {
namespace fs = std::filesystem;

enum class CheckMode { none, live, preview };

constexpr std::array live_views{"Shaded", "Visibility mask", "Bias magnitude", "Shadow-map depth"};
constexpr std::array map_sizes{256, 512, 1024, 2048};
constexpr int live_methods = static_cast<int>(Method::reference);
constexpr float study_split = 0.61f;

class ShadowResearch final : public gfx::research::Application {
public:
    ShadowResearch(fs::path shaders, fs::path heightmaps, fs::path results, fs::path check, CheckMode check_mode)
        : Application({.title = "gfx-research-shadow-auto-bias", .width = 1600, .height = 900, .vsync = false}),
          shaders_(std::move(shaders)), heightmaps_(std::move(heightmaps)), results_(std::move(results)),
          check_(std::move(check)), check_mode_(check_mode) {}

private:
    bool on_init() override {
        configure_gui();
        if (!check_.empty()) ImGui::GetIO().IniFilename = nullptr;
        run_action([&] {
            fs::create_directories(results_);
            if (check_.empty()) std::cout << "Output " << path_utf8(results_) << '\n' << std::flush;
        });
        renderer_.load_shaders(shaders_);
        load_terrain(terrain_, boxes_);
        camera_.set_clip(0.25f, 1400.0f);
        overview_camera();
        study_camera_ = camera_;
        if (check_mode_ == CheckMode::preview) start_publication(PublicationKind::quick);
        return true;
    }

    void load_terrain(int terrain, bool boxes_enabled, const fs::path& custom = {}) {
        const auto filename = custom.empty() ? heightmaps_ / (std::string(scene_names[terrain]) + ".pgm") : custom;
        const auto heightmap = load_heightmap(filename);
        renderer_.upload(make_triangles(heightmap, boxes_enabled));
        contact_height_ = static_cast<float>(heightmap.at(-77.0, 72.0)) + 2.0f;
        terrain_ = terrain;
        boxes_ = boxes_enabled;
        custom_ = custom;
    }

    template<class Action>
    void run_action(Action&& action) {
        try {
            action();
        } catch (const std::exception& error) {
            message_ = error.what();
        }
    }

    void on_drop(int count, const char** paths) override {
        if (count < 1 || timing_.running() || study_preview_) return;
        const std::string_view input(paths[0]);
        const fs::path path(std::u8string(input.begin(), input.end()));
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension != ".pgm") {
            message_ = "Choose a PGM heightmap.";
            return;
        }
        run_action([&] {
            load_terrain(terrain_, boxes_, path);
            message_ = "Loaded " + path.filename().string();
        });
    }

    void overview_camera() {
        camera_.set_target({0.0f, 5.0f, 0.0f});
        camera_.set_distance(348.0f);
        camera_.set_yaw(0.65f);
        camera_.set_pitch(0.81f);
        camera_.set_fov(49.0f);
    }

    void contact_camera() {
        camera_.set_target({-77.0f, contact_height_, 72.0f});
        camera_.set_distance(46.0f);
        camera_.set_yaw(0.65f);
        camera_.set_pitch(0.64f);
        camera_.set_fov(49.0f);
    }

    void handle_camera_input(const gfx::research::FrameInfo& frame) {
        const auto& io = ImGui::GetIO();
        if (io.WantCaptureMouse || timing_.running() || study_preview_) return;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            constexpr float rate = 0.005f;
            camera_.yaw() = std::remainder(camera_.yaw() - io.MouseDelta.x * rate,
                2.0f * std::numbers::pi_v<float>);
            camera_.pitch() = std::clamp(camera_.pitch() + io.MouseDelta.y * rate, -1.40f, 1.40f);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const auto look = glm::normalize(camera_.target() - camera_.position());
            const auto right = glm::normalize(glm::cross(look, glm::vec3(0.0f, 1.0f, 0.0f)));
            const auto up = glm::cross(right, look);
            const float scale = 2.0f * camera_.distance()
                * std::tan(camera_.fov() * std::numbers::pi_v<float> / 360.0f) / static_cast<float>(frame.height);
            camera_.target() += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * scale;
        }
        if (io.MouseWheel != 0.0f) {
            camera_.distance() = std::clamp(camera_.distance() * std::exp(-0.13f * io.MouseWheel), 4.0f, 850.0f);
        }
    }

    static glm::mat4 view_projection(const gfx::research::OrbitCamera& camera, int width, int height) {
        auto projection = camera.projection(static_cast<float>(width) / height);
        // Shift the composition towards the open area beside the floating panel.
        projection[2][0] = -std::min(0.25f, 400.0f / width);
        projection[2][1] = -0.10f;
        return projection * camera.view();
    }

    void save_screenshot(const fs::path& path, const gfx::research::FrameInfo& frame) const {
        if (!gfx::research::save_framebuffer_png(path, frame.width, frame.height))
            throw std::runtime_error("Cannot write " + path.string());
    }

    void start_timing() {
        study_preview_ = false;
        timing_.start();
        message_ = "Timing the baseline and selected method.";
    }

    void start_publication(PublicationKind kind) {
        publication_.start(kind, heightmaps_, results_ / "publication");
        study_preview_ = true;
        browsed_case_ = 0;
        study_view_ = LiveView::shaded;
        message_.clear();
    }

    void save_timing(const fs::path& path, const gfx::research::FrameInfo& frame) {
        auto out = csv_file(path);
        const auto device = gl_csv_string(GL_RENDERER);
        const auto version = gl_csv_string(GL_VERSION);
        const auto target = camera_.target();
        out << "scene,boxes,width,height,map_size,warp,sun_deg,azimuth_deg,pcf_radius,"
               "camera_x,camera_y,camera_z,distance,yaw_rad,pitch_rad,fov_deg,pair,order,side,method,parameter,"
               "draws,gpu_ms_per_draw,renderer,gl_version\n";
        for (const auto& row : timing_.rows()) {
            const auto& arm = arms_[row.side];
            out << (custom_.empty() ? scene_names[terrain_] : "custom") << ',' << boxes_ << ','
                << frame.width << ',' << frame.height << ',' << light_.resolution << ',' << light_.warp << ','
                << light_.elevation << ',' << light_.azimuth << ',' << radius_ << ','
                << target.x << ',' << target.y << ',' << target.z << ',' << camera_.distance() << ','
                << camera_.yaw() << ',' << camera_.pitch() << ',' << camera_.fov() << ','
                << row.pair << ',' << row.order << ',' << (row.side == 0 ? "baseline" : "selected") << ','
                << method_names[arm.method] << ',' << arm.parameter << ',' << timing_.draws() << ','
                << row.milliseconds << ',' << device << ',' << version << '\n';
        }
    }

    void on_frame(const gfx::research::FrameInfo& frame) override {
        if (check_mode_ == CheckMode::live) prepare_check_frame(frame);
        else if (check_mode_ == CheckMode::preview) capture_preview_check(frame);
        else handle_camera_input(frame);
        if (timing_.running() && (width_ != frame.width || height_ != frame.height)) {
            timing_.stop();
            message_ = "Window resized. Start a new timing run.";
        }
        width_ = frame.width;
        height_ = frame.height;
        sync_study_preview();
        const auto& camera = displayed_case_ ? study_camera_ : camera_;
        const auto light = displayed_case_ ? case_light(displayed_case_->setup) : light_;
        auto arms = displayed_case_ ? study_arms() : arms_;
        const int radius = displayed_case_ ? displayed_case_->setup.radius : radius_;
        auto view = displayed_case_ ? study_view_ : view_;
        int split = displayed_case_ ? static_cast<int>(width_ * study_split)
            : split_ ? static_cast<int>(width_ * split_position_) : -1;
        const auto projection = view_projection(camera, width_, height_);
        renderer_.prepare(width_, height_, projection, light);
        if (timing_.step(renderer_, arms_, radius_)) {
            const auto path = check_.empty()
                ? output_path(results_ / "timing", "live-gpu", ".csv") : check_ / "live-timing-smoke.csv";
            const auto save = [&] {
                save_timing(path, frame);
                message_ = "Saved " + path.string();
            };
            if (check_.empty()) run_action(save);
            else save();
        }
        if (const int side = timing_.active_side(); side >= 0) {
            arms[1] = arms_[side];
            view = LiveView::shaded;
            split = -1;
        }
        gpu_timer_.begin();
        renderer_.render_mask(arms, radius, view, split);
        gpu_timer_.end();
        renderer_.display(projection, camera.position(), view, split);
        if (check_mode_ == CheckMode::live) inspect_check_frame(frame);
        if (check_mode_ == CheckMode::preview) inspect_preview_check(frame);
    }

#include "publication_preview.inl"
#include "live_gui.inl"
#include "live_check.inl"

    fs::path shaders_, heightmaps_, results_, check_, custom_;
    CheckMode check_mode_;
    gfx::research::OrbitCamera camera_, study_camera_;
    LiveRenderer renderer_;
    ShadowPassTimer gpu_timer_;
    ExperimentGpuTimer timing_;
    PublicationSuite publication_;
    PublicationStatus study_status_;
    std::optional<PublicationCase> displayed_case_;
    std::optional<std::pair<std::string, bool>> study_mesh_;
    int browsed_case_ = 0;
    bool study_preview_ = false, study_footprint_ = false;
    LiveView study_view_ = LiveView::shaded;
    LightSettings light_;
    std::array<Arm, 2> arms_{{{0, default_params[0]}, {5, default_params[5]}}};
    int terrain_ = 3, radius_ = 0, width_ = 0, height_ = 0;
    float contact_height_ = 0.0f, split_position_ = 0.61f;
    LiveView view_ = LiveView::shaded;
    bool boxes_ = true, split_ = false, gui_visible_ = true, fullscreen_ = false;
    int windowed_x_ = 0, windowed_y_ = 0, windowed_width_ = 1600, windowed_height_ = 900;
    std::string message_;
    std::vector<std::uint8_t> check_auto_mask_;
    std::vector<glm::vec4> check_receivers_;
    unsigned check_map_revision_ = 0;
    int preview_check_stage_ = 0;
    bool check_running_capture_pending_ = false, check_running_captured_ = false;
};

}  // namespace lab

int main(int argc, char** argv) {
    try {
        namespace fs = std::filesystem;
        const std::string_view command = argc > 1 ? std::string_view(argv[1]) : "";
        if (command == "--help" || command == "-h") {
            std::cout << "Run without arguments to open the interactive 3D terrain.\n"
                         "  --results DIR sets the directory for new outputs.\n"
                         "  --publication-quick [--results DIR] runs the 32-case pilot and exports figures.\n"
                         "  --publication-suite [--results DIR] adds 16 production-resolution cases.\n"
                         "  --reference-viewer opens the analytical ray comparison.\n"
                         "  --live-check DIR validates the live 3D scene and saves captures.\n"
                         "  --preview-check DIR runs the quick suite and validates the test preview.\n"
                         "  --gl-check DIR compares the analytical OpenGL and CPU masks.\n"
                         "  Check commands also accept the --headless- prefix for GLFW Null with OSMesa.\n\n";
            return benchmark_main(argc, argv);
        }
        const bool live_check = command == "--live-check" || command == "--headless-live-check";
        const bool preview_check = command == "--preview-check" || command == "--headless-preview-check";
        const bool reference_check = command == "--gl-check" || command == "--headless-gl-check";
        const bool reference = command == "--reference-viewer";
        const bool publication = command == "--publication-quick" || command == "--publication-suite";
        const bool choose_results = command == "--results";
        if ((live_check || preview_check || reference_check) && argc != 3)
            throw std::runtime_error("Supply one output directory for the OpenGL check.");
        if (reference && argc != 2) throw std::runtime_error("--reference-viewer takes no arguments.");
        if (argc > 1 && !live_check && !preview_check && !reference_check && !reference && !publication && !choose_results)
            return benchmark_main(argc, argv);
        auto results = lab::default_results_directory(argv[0]);
        if (choose_results || (publication && argc > 2)) {
            const int flag = publication ? 2 : 1;
            if (argc != flag + 2 || std::string_view(argv[flag]) != "--results")
                throw std::runtime_error("Use --results DIR to select an output directory.");
            results = fs::absolute(fs::path(argv[flag + 1])).lexically_normal();
        }
        const auto maps = lab::asset_root(argv[0], "heightmaps/ramp.pgm") / "heightmaps";
        if (publication) {
            lab::PublicationSuite suite;
            suite.start(command == "--publication-quick" ? lab::PublicationKind::quick : lab::PublicationKind::full,
                maps, results / "publication");
            std::cout << "Output " << lab::path_utf8(suite.status().output) << '\n' << std::flush;
            suite.wait();
            const auto outcome = suite.status();
            std::cout << outcome.detail << '\n';
            return outcome.state == lab::PublicationState::complete ? 0 : 3;
        }
        if (command == "--headless-live-check" || command == "--headless-preview-check" || command == "--headless-gl-check") {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
            if (!glfwInit()) throw std::runtime_error("GLFW Null initialization failed.");
            glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_OSMESA_CONTEXT_API);
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        }
        if (reference || reference_check) return reference_viewer_main(argc, argv);

        const auto shaders = lab::asset_root(argv[0], "shaders/scene.vert") / "shaders";
        const auto check = live_check || preview_check ? fs::absolute(fs::path(argv[2])) : fs::path{};
        const auto mode = preview_check ? lab::CheckMode::preview : live_check ? lab::CheckMode::live : lab::CheckMode::none;
        if (preview_check) results = check / "results";
        return lab::ShadowResearch(shaders, maps, results, check, mode).run();
    } catch (const std::exception& error) {
        std::cerr << "gfx-research-shadow-auto-bias: " << error.what() << '\n';
        return 1;
    }
}
