    static void configure_gui() {
        auto& style = ImGui::GetStyle();
        style.WindowPadding = {9, 8};
        style.FramePadding = {5, 3};
        style.ItemSpacing = {7, 4};
        style.WindowRounding = 2;
        style.FrameRounding = 1;
        style.GrabRounding = 1;
        style.WindowBorderSize = 1;
        style.FrameBorderSize = 0;
        style.ScrollbarSize = 11;
        style.Colors[ImGuiCol_WindowBg] = {0.035f, 0.043f, 0.055f, 0.97f};
        style.Colors[ImGuiCol_TitleBg] = {0.09f, 0.16f, 0.25f, 1};
        style.Colors[ImGuiCol_TitleBgActive] = {0.16f, 0.28f, 0.43f, 1};
        style.Colors[ImGuiCol_FrameBg] = {0.10f, 0.16f, 0.24f, 1};
        style.Colors[ImGuiCol_FrameBgHovered] = {0.16f, 0.26f, 0.38f, 1};
        style.Colors[ImGuiCol_FrameBgActive] = {0.20f, 0.33f, 0.49f, 1};
        style.Colors[ImGuiCol_SliderGrab] = {0.24f, 0.53f, 0.85f, 1};
        style.Colors[ImGuiCol_CheckMark] = {0.40f, 0.72f, 1, 1};
        style.Colors[ImGuiCol_Header] = {0.12f, 0.23f, 0.36f, 1};
        style.Colors[ImGuiCol_HeaderHovered] = {0.20f, 0.36f, 0.54f, 1};
    }

    static bool parameter_table(const char* name) {
        if (!ImGui::BeginTable(name, 2, ImGuiTableFlags_SizingStretchProp)) return false;
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
        return true;
    }

    template<class Control>
    static void parameter(const char* label, Control&& control) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-1.0f);
        control();
        ImGui::PopID();
    }

    void handle_shortcuts() {
        const auto& io = ImGui::GetIO();
        if (io.WantTextInput || !io.KeyCtrl) return;
        if (ImGui::IsKeyPressed(ImGuiKey_H, false)) gui_visible_ = !gui_visible_;
        if (!ImGui::IsKeyPressed(ImGuiKey_F, false)) return;

        if (fullscreen_) {
            glfwSetWindowMonitor(window(), nullptr, windowed_x_, windowed_y_, windowed_width_, windowed_height_, 0);
            fullscreen_ = false;
            return;
        }
        auto* monitor = glfwGetPrimaryMonitor();
        const auto* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
        if (!mode) return;
        glfwGetWindowPos(window(), &windowed_x_, &windowed_y_);
        glfwGetWindowSize(window(), &windowed_width_, &windowed_height_);
        glfwSetWindowMonitor(window(), monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        fullscreen_ = true;
    }

    static void method_combo(Arm& arm) {
        if (ImGui::Combo("##method", &arm.method, method_labels.data(), live_methods))
            arm.parameter = default_params[arm.method];
    }

    static void bias_control(const char* label, Arm& arm) {
        parameter(label, [&] {
            const bool depth = arm.method == static_cast<int>(Method::constant)
                || arm.method == static_cast<int>(Method::receiver);
            const float limit = depth ? 0.04f : 16.0f;
            const char* format = depth ? "%.7f" : "%.3f";
            const auto flags = depth ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None;
            ImGui::SliderFloat("##bias", &arm.parameter, 0.0f, limit, format, flags | ImGuiSliderFlags_AlwaysClamp);
        });
    }

    void shadow_tab() {
        if (parameter_table("shadow")) {
            parameter("Filter", [&] {
                const char* options[]{"Hard (1 tap)", "PCF (3 x 3)"};
                ImGui::Combo("##filter", &radius_, options, IM_ARRAYSIZE(options));
            });
            bias_control("Bias parameter", arms_[1]);
            parameter("Map resolution", [&] {
                const char* options[]{"256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048"};
                const auto index = std::find(map_sizes.begin(), map_sizes.end(), light_.resolution) - map_sizes.begin();
                int choice = static_cast<int>(index);
                if (ImGui::Combo("##size", &choice, options, IM_ARRAYSIZE(options))) light_.resolution = map_sizes[choice];
            });
            parameter("Warp strength", [&] {
                ImGui::SliderInt("##warp", &light_.warp, 0, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
            });
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Comparison");
        if (parameter_table("comparison")) {
            parameter("Split comparison", [&] { ImGui::Checkbox("##split", &split_); });
            parameter("Baseline method", [&] { method_combo(arms_[0]); });
            bias_control("Baseline parameter", arms_[0]);
            ImGui::BeginDisabled(!split_);
            parameter("Divider", [&] {
                ImGui::SliderFloat("##divider", &split_position_, 0.35f, 0.88f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            });
            ImGui::EndDisabled();
            ImGui::EndTable();
        }
    }

    void scene_tab() {
        if (parameter_table("scene")) {
            parameter("Heightmap", [&] {
                const char* options[]{"Ramp", "Hills", "Terraces", "Ridge", "Custom PGM"};
                int choice = custom_.empty() ? terrain_ : 4;
                const int count = custom_.empty() ? 4 : 5;
                if (ImGui::Combo("##heightmap", &choice, options, count) && choice < 4) {
                    run_action([&] {
                        load_terrain(choice, boxes_);
                        message_.clear();
                    });
                }
            });
            parameter("Box casters", [&] {
                bool show = boxes_;
                if (ImGui::Checkbox("##boxes", &show)) {
                    run_action([&] {
                        load_terrain(terrain_, show, custom_);
                        message_.clear();
                    });
                }
            });
            parameter("Sun elevation", [&] {
                ImGui::SliderInt("##elevation", &light_.elevation, 5, 85, "%d deg", ImGuiSliderFlags_AlwaysClamp);
            });
            parameter("Sun azimuth", [&] {
                ImGui::SliderInt("##azimuth", &light_.azimuth, 0, 359, "%d deg", ImGuiSliderFlags_AlwaysClamp);
            });
            parameter("Triangles", [&] { ImGui::Text("%d", renderer_.triangle_count()); });
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Drop a PGM heightmap onto the window to open it.");
    }

    void camera_tab() {
        const auto p = camera_.position();
        ImGui::Text("Position (%.2f, %.2f, %.2f)", p.x, p.y, p.z);
        if (parameter_table("camera")) {
            parameter("Orbit distance", [&] {
                ImGui::SliderFloat("##distance", &camera_.distance(), 4.0f, 850.0f, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
            });
            parameter("Yaw", [&] {
                ImGui::SliderAngle("##yaw", &camera_.yaw(), -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            });
            parameter("Pitch", [&] {
                ImGui::SliderAngle("##pitch", &camera_.pitch(), -80.0f, 80.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
            });
            parameter("Target", [&] { ImGui::DragFloat3("##target", &camera_.target().x, 0.25f); });
            parameter("Field of view", [&] {
                ImGui::SliderFloat("##fov", &camera_.fov(), 20.0f, 105.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            });
            ImGui::EndTable();
        }
        if (ImGui::Button("Overview")) overview_camera();
        ImGui::SameLine();
        if (ImGui::Button("Contact")) contact_camera();
        ImGui::TextDisabled("Left drag orbits. Right drag pans. Scroll zooms.");
    }

    void tests_tab(const PublicationStatus& publication) {
        const bool running = publication.state == PublicationState::running;
        ImGui::SeparatorText("Publication suite");
        ImGui::TextWrapped("Quick runs 32 pilot cases. Full adds 16 cases at 2048 x 2048.");
        ImGui::TextWrapped("Each suite runs the same-plane null test and exports CSVs and comparison PNGs.");
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Run quick suite", {-1, 0}))
            run_action([&] { start_publication(PublicationKind::quick); });
        if (ImGui::Button("Run full suite", {-1, 0}))
            run_action([&] { start_publication(PublicationKind::full); });
        ImGui::EndDisabled();

        ImGui::SeparatorText("Raster bias study");
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Run raster smoke test", {-1, 0}))
            run_action([&] { start_raster("smoke"); });
        if (ImGui::Button("Run raster quick suite", {-1, 0}))
            run_action([&] { start_raster("quick"); });
        if (ImGui::Button("Run raster publication suite", {-1, 0}))
            run_action([&] { start_raster("paper"); });
        ImGui::EndDisabled();
        if (!raster_output_.empty()) {
            const auto path = path_utf8(raster_output_);
            ImGui::TextWrapped("%s", path.c_str());
            if (ImGui::Button("Copy raster output path")) ImGui::SetClipboardText(path.c_str());
        }

        study_controls();
        ImGui::SeparatorText("Output");
        const auto path = path_utf8(publication.output.empty() ? results_ : publication.output);
        ImGui::TextWrapped("%s", path.c_str());
        if (ImGui::Button("Copy output path")) ImGui::SetClipboardText(path.c_str());
        if (!running && !publication.detail.empty()) ImGui::TextWrapped("%s", publication.detail.c_str());

        ImGui::SeparatorText("GPU timing");
        ImGui::Text("Pair  %s / %s", method_labels[arms_[0].method], method_labels[arms_[1].method]);
        ImGui::TextDisabled("24 pairs, 20 fullscreen draws per sample");
        ImGui::BeginDisabled(running);
        if (ImGui::Button("Run paired timing", {-1, 0})) start_timing();
        ImGui::EndDisabled();
        ImGui::TextWrapped("Open --reference-viewer for interactive ray reference and error overlays.");
    }

    void on_gui(const gfx::research::FrameInfo& frame) override {
        handle_shortcuts();
        if (!gui_visible_) return;
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({viewport->WorkPos.x + 12, viewport->WorkPos.y + 12}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({448, std::min(800.0f, viewport->WorkSize.y - 24)}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({380, 320}, {650, std::max(320.0f, viewport->WorkSize.y - 24)});
        if (!ImGui::Begin("Shadow Research")) {
            ImGui::End();
            return;
        }

        if (raster_run_) {
            ImGui::SeparatorText("Tests");
            const auto& progress = raster_run_.progress();
            ImGui::Text("Variants %zu / %zu", progress.completed, progress.total);
            if (!progress.scene.empty())
                ImGui::Text("%s / %s", progress.scene.data(), progress.method.data());
            ImGui::ProgressBar(progress.total ? static_cast<float>(progress.completed) / progress.total : 0.0f, {-1, 0});
            ImGui::TextWrapped("%s", path_utf8(raster_output_).c_str());
            if (ImGui::Button("Stop raster study", {-1, 0}))
                finish_raster("Raster study stopped.");
            ImGui::End();
            return;
        }

        if (parameter_table("status")) {
            parameter("Frame rate", [&] { ImGui::Text("%.0f FPS (%.2f ms)", fps(), cpu_frame_milliseconds()); });
            parameter("GPU shadow pass", [&] {
                const auto view = displayed_case_ ? study_view_ : view_;
                if (view != LiveView::light_depth && gpu_timer_.has_result()) ImGui::Text("%.3f ms", gpu_timer_.milliseconds());
                else ImGui::TextUnformatted("--");
            });
            parameter("Render target", [&] { ImGui::Text("%d x %d", width_, height_); });
            ImGui::EndTable();
        }
        const auto& publication = study_status_;
        if (publication.state == PublicationState::running) {
            ImGui::SeparatorText("Publication running");
            ImGui::Text("%d / %d cases", publication.completed, publication.total);
            ImGui::ProgressBar(static_cast<float>(publication.completed) / publication.total, {-1, 0});
            ImGui::TextWrapped("%s", publication.detail.c_str());
            ImGui::BeginDisabled(publication.stopping);
            if (ImGui::Button("Stop after current case", {-1, 0})) publication_.cancel();
            ImGui::EndDisabled();
        }
        if (timing_.running()) {
            ImGui::SeparatorText("Benchmark running");
            if (const int side = timing_.active_side(); side >= 0)
                ImGui::Text("Pair %d of %d | %s", timing_.active_pair(), timing_.pair_count(), method_labels[arms_[side].method]);
            ImGui::ProgressBar(timing_.progress(), {-1, 0});
            if (ImGui::Button("Stop benchmark", {-1, 0})) {
                timing_.stop();
                message_ = "Timing stopped.";
            }
        }

        ImGui::BeginDisabled(timing_.running());
        ImGui::SeparatorText("Environment");
        if (parameter_table("environment")) {
            if (displayed_case_) {
                parameter("Shadow methods", [&] {
                    ImGui::TextUnformatted(study_footprint_ ? "Footprint / Auto" : "Constant / Auto");
                });
                parameter("Display", [&] { ImGui::TextUnformatted(live_views[static_cast<int>(study_view_)]); });
            } else {
                ImGui::BeginDisabled(study_preview_);
                parameter("Shadow method", [&] { method_combo(arms_[1]); });
                parameter("Display", [&] {
                    int selected = static_cast<int>(view_);
                    if (ImGui::Combo("##view", &selected, live_views.data(), static_cast<int>(live_views.size())))
                        view_ = static_cast<LiveView>(selected);
                });
                ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
        if (ImGui::BeginTabBar("Sections")) {
            if (ImGui::BeginTabItem("Shadow")) {
                ImGui::BeginDisabled(study_preview_);
                shadow_tab();
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Scene")) {
                ImGui::BeginDisabled(study_preview_);
                scene_tab();
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Camera")) {
                ImGui::BeginDisabled(study_preview_);
                camera_tab();
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            const bool select_tests = (check_mode_ == CheckMode::live && frame.frame_index == tests_frame)
                || (check_mode_ == CheckMode::preview && frame.frame_index < 4);
            const auto tests_flags = select_tests ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Tests", nullptr, tests_flags)) {
                tests_tab(publication);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("Reload shaders")) {
            run_action([&] {
                renderer_.load_shaders(shaders_);
                message_ = "Shaders reloaded.";
            });
        }
        ImGui::SameLine();
        if (ImGui::Button("Screenshot")) {
            run_action([&] {
                const auto path = output_path(results_ / "captures", "scene", ".png");
                save_screenshot(path, frame);
                message_ = "Saved " + path.string();
            });
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Ctrl+H: show / hide panel    Ctrl+F: fullscreen");
        if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
        study_labels();
        ImGui::End();
    }
