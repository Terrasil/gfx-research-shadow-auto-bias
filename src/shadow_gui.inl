    static void configure_gui() {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.Fonts->AddFontDefaultVector();
        auto& style = ImGui::GetStyle();
        style.FontSizeBase = 15;
        style.WindowPadding = {12,12};
        style.FramePadding = {6,4};
        style.ItemSpacing = {8,6};
        style.WindowRounding = 3;
        style.FrameRounding = 2;
        style.Colors[ImGuiCol_WindowBg] = {0.050f,0.066f,0.086f,1};
        style.Colors[ImGuiCol_FrameBg] = {0.087f,0.133f,0.187f,1};
        style.Colors[ImGuiCol_FrameBgHovered] = {0.160f,0.263f,0.382f,1};
        style.Colors[ImGuiCol_Header] = {0.113f,0.226f,0.359f,1};
        style.Colors[ImGuiCol_TabSelected] = {0.15f,0.27f,0.40f,1};
        style.Colors[ImGuiCol_Button] = {0.13f,0.27f,0.43f,1};
        style.Colors[ImGuiCol_SliderGrab] = {0.27f,0.54f,0.81f,1};
        style.Colors[ImGuiCol_CheckMark] = {0.40f,0.72f,1.0f,1};
    }

    static bool parameter_table(const char* id) {
        if (!ImGui::BeginTable(id,2,ImGuiTableFlags_SizingStretchProp)) return false;
        ImGui::TableSetupColumn("Setting",ImGuiTableColumnFlags_WidthStretch,0.43f);
        ImGui::TableSetupColumn("Value",ImGuiTableColumnFlags_WidthStretch,0.57f);
        return true;
    }

    template<class Widget> static void parameter(const char* label, Widget&& widget) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-1.0f);
        widget();
        ImGui::PopID();
    }

    void method_ui(int side) {
        auto& arm = arms_[side];
        if (ImGui::Combo("##method",&arm.method,method_labels.data(),static_cast<int>(method_labels.size()))) {
            arm.parameter = default_params[arm.method];
            preview_dirty_ = true;
        }
    }

    void bias_ui(int side) {
        auto& arm = arms_[side];
        if (arm.method==static_cast<int>(Method::reference)) { ImGui::TextDisabled("Ray reference"); return; }
        const bool scaled = arm.method==static_cast<int>(Method::slope) || arm.method>=static_cast<int>(Method::numeric);
        if (ImGui::InputFloat("##bias",&arm.parameter,0,0,scaled ? "%.3f" : "%.6g")) {
            const float max = scaled ? 8.0f : 0.05f;
            arm.parameter = std::isfinite(arm.parameter) ? std::clamp(arm.parameter,0.0f,max)
                : default_params[arm.method];
            preview_dirty_ = true;
        }
    }

    void shadow_tab() {
        if (!parameter_table("shadow")) return;
        parameter("Left method",[&]{method_ui(0);});
        parameter("Left parameter",[&]{bias_ui(0);});
        parameter("Right method",[&]{method_ui(1);});
        parameter("Right parameter",[&]{bias_ui(1);});
        parameter("Filter",[&]{
            const char* labels[]{"Hard","3 x 3 PCF"};
            if (ImGui::Combo("##filter",&radius_,labels,2)) preview_dirty_=true;
        });
        parameter("Display",[&]{
            if (ImGui::Combo("##display",&view_,view_labels.data(),static_cast<int>(view_labels.size())))
                preview_dirty_=true;
        });
        ImGui::EndTable();
        ImGui::TextDisabled("Constant / receiver: depth units.");
        ImGui::TextDisabled("Slope multiplier or method scale.");
    }

    void scene_tab() {
        if (parameter_table("scene")) {
            parameter("Terrain",[&]{
                if (ImGui::Combo("##terrain",&edit_.terrain,scene_names.data(),static_cast<int>(scene_names.size())))
                    edit_.custom.clear();
            });
            parameter("Sun elevation",[&]{ImGui::SliderInt("##sun",&edit_.sun,5,85,"%d deg");});
            parameter("Sun azimuth",[&]{ImGui::SliderInt("##az",&edit_.azimuth,0,359,"%d deg");});
            parameter("Warp strength",[&]{ImGui::SliderInt("##warp",&edit_.alpha,0,32);});
            parameter("Shadow map",[&]{
                constexpr std::array values{128,256,512,1024,2048};
                const char* labels[]{"128","256","512","1024","2048"};
                int index=static_cast<int>(std::find(values.begin(),values.end(),edit_.size)-values.begin());
                if (ImGui::Combo("##size",&index,labels,static_cast<int>(values.size())))
                    edit_.size=values[index];
            });
            parameter("Box casters",[&]{ImGui::Checkbox("##objects",&edit_.objects);});
            ImGui::EndTable();
        }
        if (!edit_.custom.empty()) ImGui::TextWrapped("%s",edit_.custom.filename().string().c_str());
        ImGui::TextDisabled("Drop a PGM onto the window to import terrain.");
        if (ImGui::Button("Apply scene",{-1,0})) request_scene();
    }

    void camera_tab() {
        if (parameter_table("camera")) {
            parameter("View",[&]{
                int view=edit_.contacts ? 1 : 0;
                const char* labels[]{"Overview","Contact detail"};
                if (ImGui::Combo("##view",&view,labels,2)) edit_.contacts=view==1;
            });
            parameter("Yaw offset",[&]{ImGui::SliderInt("##yaw",&edit_.yaw,-180,180,"%d deg");});
            parameter("Pitch offset",[&]{ImGui::SliderInt("##pitch",&edit_.pitch,-42,30,"%d deg");});
            parameter("Zoom",[&]{ImGui::SliderFloat("##zoom",&edit_.zoom,0.5f,3.0f,"%.2fx");});
            parameter("Output size",[&]{
                const char* labels[]{"480 x 360","640 x 480","960 x 720","1280 x 720","1920 x 1080"};
                constexpr std::array sizes{std::pair{480,360},std::pair{640,480},std::pair{960,720},
                    std::pair{1280,720},std::pair{1920,1080}};
                int selected=static_cast<int>(std::find(sizes.begin(),sizes.end(),std::pair{edit_.width,edit_.height})-sizes.begin());
                if (ImGui::Combo("##output",&selected,labels,static_cast<int>(sizes.size()))) {
                    edit_.width=sizes[selected].first;
                    edit_.height=sizes[selected].second;
                }
            });
            ImGui::EndTable();
        }
        if (ImGui::Button("Apply camera",{-1,0})) request_scene();
    }

    void tests_tab() {
        const int reference = static_cast<int>(Method::reference);
        ImGui::BeginDisabled(arms_[0].method==reference || arms_[1].method==reference);
        if (ImGui::Button("Run paired GPU timing",{-1,0})) timing_.start();
        ImGui::EndDisabled();
        ImGui::TextWrapped("24 pairs in alternating order and 20 draws per sample. Use two shadow-map methods.");
        if (ImGui::Button("Save PNG and quality CSV",{-1,0})) {
            try {
                const auto image = output_path(results_ / "captures","comparison",".png");
                renderer_.save_comparison(image);
                auto quality = image;
                quality.replace_extension(".csv");
                save_quality(quality);
                message_ = "Saved "+image.string()+" and its CSV.";
            } catch (const std::exception& error) { message_ = error.what(); }
        }
    }

    static void metric(std::uint64_t count, std::uint64_t total) {
        if (total==0) ImGui::TextDisabled("n/a");
        else ImGui::Text("%.2f%%",100.0*static_cast<double>(count)/static_cast<double>(total));
    }

    void draw_image(int side) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(method_labels[arms_[side].method]);
        const auto& s = current_->settings;
        const float available=ImGui::GetContentRegionAvail().x;
        const float ratio=static_cast<float>(s.width)/s.height;
        const float h=std::max(60.0f,std::min(available/ratio,ImGui::GetContentRegionAvail().y-80.0f));
        const auto texture=static_cast<ImTextureID>(renderer_.preview(side));
        ImGui::Image(texture,{h*ratio,h},{0,1},{1,0});
        ImGui::PushID(side);
        if (ImGui::BeginTable("errors",3,ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("False shadows");
            ImGui::TableSetupColumn("Missed shadows");
            ImGui::TableSetupColumn("Lost contacts");
            ImGui::TableHeadersRow();
            const auto& c = scores_[side];
            ImGui::TableNextColumn(); metric(c.fp,c.lit);
            ImGui::TableNextColumn(); metric(c.fn,c.shadow);
            ImGui::TableNextColumn(); metric(c.contact_fn,c.contact);
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    void on_gui(const gfx::research::FrameInfo&) override {
        const auto* window=ImGui::GetMainViewport();
        const float width=std::min(390.0f,window->WorkSize.x*0.36f);
        const float height=std::max(120.0f,window->WorkSize.y-20.0f);
        constexpr auto flags=ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse;
        ImGui::SetNextWindowPos({window->WorkPos.x+10,window->WorkPos.y+10});
        ImGui::SetNextWindowSize({width,height});
        if (ImGui::Begin("Shadow Map Bias",nullptr,flags)) {
            const auto& s=current_->settings;
            ImGui::Text("%d x %d view, %d x %d shadow map",s.width,s.height,s.size,s.size);
            if (timing_.running()) {
                ImGui::ProgressBar(timing_.progress(),{-1,0});
                if (ImGui::Button("Stop GPU timing",{-1,0})) timing_.stop();
            }
            ImGui::BeginDisabled(timing_.running() || pending_.valid());
            if (ImGui::BeginTabBar("settings")) {
                if (ImGui::BeginTabItem("Shadow")) { shadow_tab(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Scene")) { scene_tab(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Camera")) { camera_tab(); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Tests")) { tests_tab(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
            ImGui::EndDisabled();
            if (!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        }
        ImGui::End();

        ImGui::SetNextWindowPos({window->WorkPos.x+width+18,window->WorkPos.y+10});
        ImGui::SetNextWindowSize({std::max(120.0f,window->WorkSize.x-width-28.0f),height});
        if (ImGui::Begin("Comparison",nullptr,flags)) {
            const auto& s=current_->settings;
            const auto* terrain=s.custom.empty() ? scene_names[s.terrain] : "custom terrain";
            ImGui::TextDisabled("%s   |   Sun %d deg   |   Warp %d   |   %s",terrain,s.sun,s.alpha,
                s.contacts ? "Contact detail" : "Overview");
            if (view_==1) ImGui::TextDisabled("Orange: false shadow. Cyan: missed shadow.");
            if (ImGui::BeginTable("views",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)) {
                draw_image(0);
                draw_image(1);
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
