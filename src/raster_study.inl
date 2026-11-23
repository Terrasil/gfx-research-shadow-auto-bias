    void start_raster(std::string profile) {
        const std::string stem = "raster-" + profile;
        raster_output_ = fs::absolute(output_path(results_ / "raster", stem.c_str(), ""));
        raster_run_ = run_raster_study(std::move(profile), raster_output_);
        study_preview_ = false;
        displayed_case_.reset();
        study_mesh_.reset();
        message_.clear();
    }

    void finish_raster(std::string message) {
        raster_run_.reset();
        renderer_.use_mesh(MeshSlot::scene);
        message_ = std::move(message);
    }

    void advance_raster(const gfx::research::FrameInfo& frame) {
        try {
            if (!raster_run_.advance()) finish_raster("Raster study complete.");
        } catch (const std::exception& error) {
            finish_raster(error.what());
        }
        gfx::research::Framebuffer::bind_default();
        glViewport(0, 0, frame.width, frame.height);
        glClearColor(0.06f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    ExperimentRun run_raster_study(std::string profile, fs::path output) {
        const bool smoke = profile == "smoke";
        const bool paper = profile == "paper";
        if (fs::exists(output) && !fs::is_empty(output))
            throw std::runtime_error("Choose an empty raster-study output directory.");
        auto report = csv_file(output / "raster_quality.csv");
        auto timing = csv_file(output / "raster_timing.csv");
        auto manifest = csv_file(output / "raster_manifest.txt");
        constexpr std::array<int,4> scene_indices{0,1,2,3};
        const std::vector<int> sizes = paper ? std::vector{1024,2048} : std::vector{smoke ? 256 : 512};
        const std::vector<int> elevations = smoke ? std::vector{18} : std::vector{18,38};
        const std::vector<int> warps = smoke ? std::vector{8} : std::vector{0,8};
        const std::vector<int> radii = paper ? std::vector{0,1} : std::vector{0};
        const int width = smoke ? 192 : paper ? 1280 : 480;
        const int height = smoke ? 144 : paper ? 720 : 360;
        const int views = smoke ? 1 : 2;
        const int samples = smoke ? 2 : paper ? 20 : 5;
        const int jobs = static_cast<int>(std::clamp(std::thread::hardware_concurrency(),1u,8u));
        const std::size_t planned = scene_indices.size()*sizes.size()*elevations.size()*warps.size()*radii.size()*views;
        manifest << "profile=" << profile << "\nplanned=" << planned << "\nwidth=" << width << "\nheight=" << height
                 << "\nsamples=" << samples << "\nrenderer=" << gl_csv_string(GL_RENDERER)
                 << "\ngl_version=" << gl_csv_string(GL_VERSION) << "\nprimary_ray_skip=1e-6\n";
        manifest.flush();
        report << "case,scene,split,view,resolution,warp,elevation,pcf_radius,method,parameter,eligible,excluded,"
                  "lit,shadow,false_shadow,missed_shadow,contact,missed_contact\n";
        timing << "case,method,parameter,sample,gpu_ns\n";

        std::vector<Arm> arms;
        const auto add = [&arms](Method method, std::initializer_list<float> values) {
            for (float value : values) arms.push_back({static_cast<int>(method),value});
        };
        add(Method::constant,{0.000003f,0.00001f,0.00003f,0.0001f,0.0003f,0.001f});
        add(Method::slope,{0.1f,0.3f,0.5f,0.85f,1.5f});
        add(Method::receiver,{0.0000005f,0.000002f,0.00001f,0.00003f,0.0001f,0.0003f});
        add(Method::numeric,{2.0f});
        add(Method::footprint,{1.0f,2.0f,3.0f});
        add(Method::full,{1.0f,2.0f,3.0f});
        add(Method::selective,{1.0f,2.0f,3.0f});
        manifest << "arms=" << arms.size() << "\ncompleted=0\n" << std::flush;
        co_yield (ExperimentProgress{0, planned * arms.size(), {}, {}});

        struct ElapsedQuery {
            GLuint id = 0;
            ~ElapsedQuery() { if (id) glDeleteQueries(1, &id); }
        } query;
        glCreateQueries(GL_TIME_ELAPSED, 1, &query.id);
        auto orbit = camera_;
        std::size_t index=0;
        for (int scene : scene_indices) {
            const auto heightmap = load_heightmap(heightmaps_ / (std::string(scene_names[scene]) + ".pgm"));
            const Bvh mesh = make_mesh(heightmap, true);
            renderer_.upload(mesh.triangles, MeshSlot::study);
            renderer_.use_mesh(MeshSlot::study);
            const float contact_height = static_cast<float>(heightmap.at(-77.0, 72.0)) + 2.0f;
            for (int w : warps) for (int n : sizes) for (int sun : elevations) for (int camera=0; camera<views; ++camera) {
                if (camera == 0) set_overview_camera(orbit);
                else set_contact_camera(orbit, contact_height);
                const LightSettings settings{sun,37,n,w};
                const Light light(sun,37);
                // Use the native view without the interactive panel's projection offset.
                const glm::mat4 projection=orbit.view_projection(static_cast<float>(width)/height);
                renderer_.prepare(width,height,projection,settings);
                const auto receivers=renderer_.read_receivers();
                const glm::dmat4 inverse=glm::inverse(glm::dmat4(projection));
                const auto eye=orbit.position();
                const Vec origin{eye.x,eye.y,eye.z};
                std::vector<std::uint8_t> classes(receivers.size());
                parallel_rows(height,jobs,[&](const int y) {
                    for (int x=0;x<width;++x) {
                        const auto i=static_cast<std::size_t>(y)*width+x;
                        const auto p=receivers[i];
                        if (p.w<2.0f) continue;
                        const glm::dvec4 clip((2.0*x+1)/width-1.0,(2.0*y+1)/height-1.0,0.0,1.0);
                        const glm::dvec4 far=inverse*clip;
                        const Vec target{far.x/far.w,far.y/far.w,far.z/far.w};
                        const Ray ray(origin,normalized(target-origin));
                        const auto hit=mesh.intersect(ray,0.25,1400.0);
                        if (hit.triangle<0) continue;
                        const Vec point=origin+ray.direction*hit.distance;
                        const auto lp=light.project(point);
                        if (std::max({std::abs(lp.x-p.x),std::abs(lp.y-p.y),std::abs(lp.z-p.z)})>5e-5) continue;
                        const auto blocker=mesh.intersect(Ray(point,light.to_sun),1e-6,depth_range,hit.triangle);
                        classes[i]=blocker.triangle<0?2:3;
                        if (blocker.triangle>=0 && mesh.triangles[hit.triangle].object==0) {
                            const auto object=mesh.triangles[blocker.triangle].object;
                            if (object>0 && boxes[object-1].horizontal_distance(point)<=2.0) classes[i]=4;
                        }
                    }
                });
                const auto eligible=std::count_if(receivers.begin(),receivers.end(),[](glm::vec4 p){return p.w>=2.0f;});
                const auto excluded=eligible-std::count_if(classes.begin(),classes.end(),[](std::uint8_t p){return p>=2;});
                if (eligible<100 || excluded*100>eligible)
                    throw std::runtime_error("Raster and BVH receivers diverge in more than 1% of eligible pixels.");
                for (int radius : radii) {
                    if (scene==3 && w==warps.back() && n==sizes.back() && sun==18 && camera==0) {
                        std::ofstream image(output/("reference-pcf"+std::to_string(radius)+".pgm"),std::ios::binary);
                        image << "P5\n" << width << ' ' << height << "\n4\n";
                        for (int y=height-1;y>=0;--y)
                            image.write(reinterpret_cast<const char*>(classes.data()+static_cast<std::size_t>(y)*width),width);
                        if (!image) throw std::runtime_error("Could not save raster reference mask.");
                    }
                    for (std::size_t arm_index=0;arm_index<arms.size();++arm_index) {
                        const auto arm=arms[(arm_index+index)%arms.size()];
                        renderer_.bind_mask(arm,radius,false);
                        for (int warm=0;warm<(smoke?1:6);++warm) renderer_.draw();
                        glFinish();
                        for (int sample=0;sample<samples;++sample) {
                            glBeginQuery(GL_TIME_ELAPSED,query.id);
                            renderer_.draw();
                            glEndQuery(GL_TIME_ELAPSED);
                            GLuint64 elapsed=0;
                            glGetQueryObjectui64v(query.id,GL_QUERY_RESULT,&elapsed);
                            if (elapsed==0) throw std::runtime_error("Invalid raster-study GPU time.");
                            timing << index << ',' << method_names[arm.method] << ',' << arm.parameter << ',' << sample
                                   << ',' << elapsed << '\n';
                        }
                        const auto mask=renderer_.read_mask();
                        Counts score;
                        for (std::size_t i=0;i<classes.size();++i) score.add(classes[i],mask[i]>=128);
                        report << index << ',' << scene_names[scene] << ',' << (scene==0?"calibration":"held-out") << ','
                               << (camera==0?"overview":"contact") << ',' << n << ',' << w << ',' << sun << ','
                               << radius << ',' << method_names[arm.method] << ',' << arm.parameter << ','
                               << eligible << ',' << excluded;
                        score.write(report);
                        report << '\n';
                        co_yield (ExperimentProgress{index * arms.size() + arm_index + 1,
                            planned * arms.size(), scene_names[scene], method_names[arm.method]});
                    }
                    std::cout << "Raster bias " << ++index << '/' << planned << "  " << scene_names[scene] << '\n' << std::flush;
                }
            }
        }
        if (glGetError()!=GL_NO_ERROR || index!=planned) throw std::runtime_error("Incomplete raster-study run.");
        report.close(); timing.close();
        manifest << "completed=1\n";
    }
