    static constexpr std::uint64_t constant_frame = 6;
    static constexpr std::uint64_t orbit_frame = constant_frame + 1;
    static constexpr std::uint64_t split_frame = orbit_frame + 1;
    static constexpr std::uint64_t uniform_frame = split_frame + 1;
    static constexpr std::uint64_t tests_frame = uniform_frame + 1;
    static constexpr std::uint64_t timing_frame = tests_frame + 3;

    void capture_previous_frame(const fs::path& name, const gfx::research::FrameInfo& frame) const {
        GLint read_buffer = GL_BACK;
        glGetIntegerv(GL_READ_BUFFER, &read_buffer);
        glReadBuffer(GL_FRONT);
        save_screenshot(check_ / name, frame);
        glReadBuffer(read_buffer);
    }

    void prepare_check_frame(const gfx::research::FrameInfo& frame) {
        if (frame.frame_index == constant_frame) {
            capture_previous_frame("control-panel.png", frame);
            arms_[1] = {0, default_params[0]};
        } else if (frame.frame_index == orbit_frame) {
            arms_[1] = {5, default_params[5]};
            camera_.yaw() += 0.68f;
            camera_.pitch() += 0.12f;
        } else if (frame.frame_index == split_frame) {
            overview_camera();
            split_ = true;
        } else if (frame.frame_index == uniform_frame) {
            capture_previous_frame("control-panel-split.png", frame);
            split_ = false;
            light_.warp = 0;
            light_.azimuth = 113;
            load_terrain(2, false);
        } else if (frame.frame_index == tests_frame) {
            light_ = {};
            load_terrain(3, true);
        } else if (frame.frame_index == timing_frame) {
            capture_previous_frame("tests-panel.png", frame);
            timing_.start(2, 2);
        }
    }

    void check_light_map(const fs::path& filename) const {
        const auto map = make_mesh(load_heightmap(heightmaps_ / (std::string(scene_names[terrain_]) + ".pgm")), boxes_);
        const Light sun(light_.elevation, light_.azimuth);
        const int jobs = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
        const ShadowMap reference(map, sun, light_.resolution, light_.warp, jobs);
        const auto gpu = renderer_.read_depth();
        std::size_t covered = 0, coverage_mismatches = 0, depth_mismatches = 0;
        double largest_difference = 0.0;
        for (std::size_t i = 0; i < gpu.size(); ++i) {
            const float expected = reference.texels[i].depth;
            covered += expected < 1.0f;
            if ((expected < 1.0f) != (gpu[i] < 1.0f)) {
                ++coverage_mismatches;
            } else if (expected < 1.0f) {
                const double difference = std::abs(static_cast<double>(gpu[i]) - expected);
                largest_difference = std::max(largest_difference, difference);
                depth_mismatches += difference > 1e-6;
            }
        }
        auto out = csv_file(check_ / filename);
        out << "texels,reference_covered,coverage_mismatches,depth_mismatches_above_1e-6,largest_matched_depth_difference\n"
            << gpu.size() << ',' << covered << ',' << coverage_mismatches << ',' << depth_mismatches
            << ',' << largest_difference << '\n';
        if (covered < 1000 || (coverage_mismatches + depth_mismatches) * 1000 > covered)
            throw std::runtime_error("The rasterized shadow map disagrees with the ray-sampled map.");
    }

    void inspect_check_frame(const gfx::research::FrameInfo& frame) {
        if (const auto error = glGetError(); error != GL_NO_ERROR)
            throw std::runtime_error("OpenGL live check failed with error " + std::to_string(error) + ".");

        if (frame.frame_index == 0) {
            check_receivers_ = renderer_.read_receivers();
            const auto eligible = std::count_if(check_receivers_.begin(), check_receivers_.end(),
                [](glm::vec4 pixel) { return pixel.w >= 2.0f; });
            if (eligible < 10000) throw std::runtime_error("Too little of the live terrain is visible.");
            check_auto_mask_ = renderer_.read_mask();
            if (std::count(check_auto_mask_.begin(), check_auto_mask_.end(), 255) < 500)
                throw std::runtime_error("Auto produced an empty shadow mask.");
            check_map_revision_ = renderer_.map_revision();
            check_light_map("warped-depth-check.csv");
            save_screenshot(check_ / "scene-auto.png", frame);
        } else if (frame.frame_index == constant_frame) {
            const auto mask = renderer_.read_mask();
            const auto different = std::inner_product(check_auto_mask_.begin(), check_auto_mask_.end(), mask.begin(),
                std::size_t{0}, std::plus<>{}, std::not_equal_to<>{});
            if (different < 1000) throw std::runtime_error("Changing the bias method did not affect the live scene.");
            auto out = csv_file(check_ / "live-check.csv");
            out << "width,height,triangles,method_mask_changes\n"
                << frame.width << ',' << frame.height << ',' << renderer_.triangle_count() << ',' << different << '\n';
            save_screenshot(check_ / "scene-constant.png", frame);
        } else if (frame.frame_index == orbit_frame) {
            const auto moved = renderer_.read_receivers();
            const auto different = std::inner_product(check_receivers_.begin(), check_receivers_.end(), moved.begin(),
                std::size_t{0}, std::plus<>{}, [](glm::vec4 a, glm::vec4 b) { return (a.w > 0.0f) != (b.w > 0.0f); });
            if (different < 5000 || renderer_.map_revision() != check_map_revision_)
                throw std::runtime_error("Orbit camera coverage or shadow-map caching failed.");
            save_screenshot(check_ / "scene-orbit.png", frame);
        } else if (frame.frame_index == split_frame) {
            const auto split_mask = renderer_.read_mask();
            const auto split_x = static_cast<int>(frame.width * split_position_);
            std::size_t left_differences = 0, right_differences = 0;
            for (std::size_t i = 0; i < split_mask.size(); ++i) {
                if (split_mask[i] == check_auto_mask_[i]) continue;
                if (static_cast<int>(i % frame.width) < split_x) ++left_differences;
                else ++right_differences;
            }
            if (left_differences < 1000 || right_differences != 0)
                throw std::runtime_error("The split comparison did not preserve Auto on the selected side.");
            save_screenshot(check_ / "scene-split.png", frame);
        } else if (frame.frame_index == uniform_frame) {
            if (renderer_.map_revision() == check_map_revision_)
                throw std::runtime_error("Changing the terrain and light did not rebuild the map.");
            check_light_map("uniform-depth-check.csv");
        } else if (frame.frame_index > timing_frame && !timing_.running()) {
            if (timing_.rows().size() != 4) throw std::runtime_error("Incomplete live GPU timing.");
            close();
        } else if (frame.frame_index > 120) {
            throw std::runtime_error("The live GPU timer did not finish.");
        }
    }

    void capture_preview_check(const gfx::research::FrameInfo& frame) {
        if (check_running_capture_pending_) {
            capture_previous_frame("publication-running.png", frame);
            check_running_capture_pending_ = false;
            check_running_captured_ = true;
        }
        if (preview_check_stage_ == 2) {
            capture_previous_frame("publication-preview.png", frame);
            browsed_case_ = 1;
            preview_check_stage_ = 3;
        }
    }

    void inspect_preview_check(const gfx::research::FrameInfo& frame) {
        if (const auto error = glGetError(); error != GL_NO_ERROR)
            throw std::runtime_error("OpenGL preview check failed with error " + std::to_string(error) + ".");
        if (study_status_.state == PublicationState::failed)
            throw std::runtime_error(study_status_.detail);
        if (frame.time_seconds > 90.0) throw std::runtime_error("Publication preview check timed out.");

        if (!check_running_captured_ && frame.frame_index >= 3 && displayed_case_ && displayed_case_->number > 8
                && study_status_.state == PublicationState::running) {
            check_running_capture_pending_ = true;
        }

        if (preview_check_stage_ == 0 && study_status_.state == PublicationState::complete) {
            if (study_status_.completed != study_case_count(StudyProfile::pilot))
                throw std::runtime_error("The publication preview received an incomplete suite.");
            browsed_case_ = 28;
            preview_check_stage_ = 1;
        } else if (preview_check_stage_ == 1 && displayed_case_ && displayed_case_->number == 28) {
            const auto& scene = displayed_case_->setup;
            if (!displayed_case_->scores || scene.scene != "ridge" || scene.elevation != 18
                    || scene.warp != 8 || scene.resolution != 512 || renderer_.read_depth().size() != 512u * 512u)
                throw std::runtime_error("The preview did not load the selected case.");
            auto csv = csv_file(check_ / "preview-scores.csv");
            csv << "method,lit,shadow,false_shadow,missed_shadow,contact,missed_contact\n";
            for (std::size_t i = 0; i < study_preview_methods.size(); ++i) {
                csv << method_names[static_cast<int>(study_preview_methods[i])];
                (*displayed_case_->scores)[i].write(csv);
                csv << '\n';
            }
            preview_check_stage_ = 2;
        } else if (preview_check_stage_ == 3 && displayed_case_ && displayed_case_->number == 1) {
            if (displayed_case_->setup.scene != "ramp" || renderer_.read_depth().size() != 256u * 256u)
                throw std::runtime_error("The preview did not switch to the first case.");
            study_preview_ = false;
            preview_check_stage_ = 4;
        } else if (preview_check_stage_ == 4 && !displayed_case_) {
            if (renderer_.read_depth().size() != static_cast<std::size_t>(light_.resolution) * light_.resolution)
                throw std::runtime_error("The scene was not restored after closing the test preview.");
            close();
        }
    }
