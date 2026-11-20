    static LightSettings case_light(const StudyCase& setup) {
        return {setup.elevation, setup.azimuth, setup.resolution, setup.warp};
    }

    std::array<Arm, 2> study_arms() const {
        const int baseline = static_cast<int>(study_footprint_ ? Method::footprint : Method::constant);
        const int automatic = static_cast<int>(Method::full);
        return {{{baseline, default_params[baseline]}, {automatic, default_params[automatic]}}};
    }

    void sync_study_preview() {
        study_status_ = publication_.status();
        displayed_case_.reset();
        if (study_preview_) {
            displayed_case_ = browsed_case_ > 0 ? publication_.completed_case(browsed_case_) : study_status_.current;
        }
        if (!displayed_case_) {
            renderer_.use_mesh(MeshSlot::scene);
            return;
        }

        const auto& setup = displayed_case_->setup;
        const auto key = std::pair(setup.scene, setup.boxes);
        try {
            if (study_mesh_ != key) {
                const auto heightmap = load_heightmap(heightmaps_ / (setup.scene + ".pgm"));
                renderer_.upload(make_triangles(heightmap, setup.boxes), MeshSlot::study);
                study_mesh_ = key;
            }
            renderer_.use_mesh(MeshSlot::study);
        } catch (const std::exception& error) {
            displayed_case_.reset();
            study_preview_ = false;
            renderer_.use_mesh(MeshSlot::scene);
            message_ = error.what();
        }
    }

    void study_scores(const StudyScores& scores) {
        constexpr auto flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("Case scores", 4, flags)) return;
        ImGui::TableSetupColumn("Ray-reference errors", ImGuiTableColumnFlags_WidthStretch, 1.9f);
        ImGui::TableSetupColumn("Constant");
        ImGui::TableSetupColumn("Footprint");
        ImGui::TableSetupColumn("Auto");
        ImGui::TableHeadersRow();
        const auto row = [&](const char* label, std::uint64_t Counts::*errors, std::uint64_t Counts::*total) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            for (int column = 0; column < static_cast<int>(scores.size()); ++column) {
                ImGui::TableSetColumnIndex(column + 1);
                const auto& count = scores[column];
                if (count.*total) ImGui::Text("%.2f%%", 100.0 * (count.*errors) / (count.*total));
                else ImGui::TextDisabled("n/a");
            }
        };
        row("False shadows", &Counts::fp, &Counts::lit);
        row("Missed shadows", &Counts::fn, &Counts::shadow);
        row("Missed contacts", &Counts::contact_fn, &Counts::contact);
        ImGui::EndTable();
    }

    void study_controls() {
        if (!study_status_.current && !study_preview_) return;
        ImGui::SeparatorText("Test preview");
        if (!study_preview_) {
            if (ImGui::Button("Show test preview", {-1, 0})) study_preview_ = true;
            return;
        }

        if (ImGui::Button("Back to scene")) study_preview_ = false;
        ImGui::SameLine();
        ImGui::BeginDisabled(browsed_case_ == 0);
        if (ImGui::Button("Follow latest case")) browsed_case_ = 0;
        ImGui::EndDisabled();
        if (!displayed_case_) return;

        const auto& test = *displayed_case_;
        const auto& setup = test.setup;
        const char* profile = test.profile == StudyProfile::pilot ? "Pilot" : "Production";
        if (test.number == 0)
            ImGui::Text("%s / Same-plane null", profile);
        else
            ImGui::Text("%s / Case %d of %d", profile, test.number, study_status_.total);
        ImGui::Text("%s | %d x %d | warp %d", setup.scene.c_str(), setup.resolution, setup.resolution, setup.warp);
        ImGui::Text("Sun %d deg | azimuth %d deg | %s", setup.elevation, setup.azimuth, setup.radius ? "PCF" : "Hard");
        if (parameter_table("preview controls")) {
            parameter("Display", [&] {
                int view = static_cast<int>(study_view_);
                if (ImGui::Combo("##view", &view, live_views.data(), static_cast<int>(live_views.size())))
                    study_view_ = static_cast<LiveView>(view);
            });
            parameter("Compare Auto with", [&] {
                const char* options[]{"Constant", "Footprint"};
                int baseline = study_footprint_ ? 1 : 0;
                if (ImGui::Combo("##baseline", &baseline, options, IM_ARRAYSIZE(options)))
                    study_footprint_ = baseline == 1;
            });
            ImGui::EndTable();
        }

        const int index = test.number == 0 ? study_status_.completed + 1 : test.number;
        ImGui::BeginDisabled(index <= 1 || study_status_.completed == 0);
        if (ImGui::Button("Previous case")) browsed_case_ = std::min(index - 1, study_status_.completed);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index >= study_status_.completed);
        if (ImGui::Button("Next case")) browsed_case_ = index + 1;
        ImGui::EndDisabled();

        if (test.scores) study_scores(*test.scores);
        else if (test.number != 0) ImGui::TextDisabled("Waiting for ray-reference scores.");
    }

    void study_labels() const {
        if (!displayed_case_ || study_view_ == LiveView::light_depth) return;
        const auto* viewport = ImGui::GetMainViewport();
        const auto panel_pos = ImGui::GetWindowPos();
        const auto panel_width = ImGui::GetWindowWidth();
        const float x0 = viewport->Pos.x;
        const float x1 = x0 + viewport->Size.x * study_split;
        const float x2 = x0 + viewport->Size.x;
        const float left = std::max(x0, panel_pos.x + panel_width);
        const float y = viewport->Pos.y + 24.0f;
        auto* draw = ImGui::GetForegroundDrawList();
        const auto label = [&](const char* name, float low, float high) {
            const auto size = ImGui::CalcTextSize(name);
            if (high - low < size.x + 20.0f) return;
            const float x = (low + high - size.x) * 0.5f;
            draw->AddRectFilled({x - 9.0f, y - 5.0f}, {x + size.x + 9.0f, y + size.y + 5.0f},
                IM_COL32(15, 28, 43, 245), 3.0f);
            draw->AddText({x, y}, IM_COL32_WHITE, name);
        };
        label(study_footprint_ ? "Footprint" : "Constant", left, x1);
        label("Auto", std::max(x1, left), x2);
    }
