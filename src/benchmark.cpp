#include "asset_root.hpp"
#include "benchmark.hpp"

#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using namespace lab;
namespace fs = std::filesystem;

constexpr std::string_view version = "0.1.1";
constexpr int image_width = 480, image_height = 360;

struct Config {
    std::string scene;
    int sun = 18, azimuth = 37, size = 512, alpha = 8, radius = 0;
    bool contacts = false, null_scene = false;

    StudyCase preview() const {
        return {scene, sun, azimuth, size, alpha, radius, !null_scene};
    }

    std::string id() const {
        return scene+"-sun"+std::to_string(sun)+"-az"+std::to_string(azimuth)
            +"-n"+std::to_string(size)+"-w"+std::to_string(alpha)+(contacts ? "-contact" : "-overview")
            +(null_scene ? "-null" : "");
    }
};

struct Variant {
    Method method;
    float value;
};

std::vector<Variant> sweep() {
    std::vector<Variant> result;
    for (float b : {0.0f,0.000001f,0.000003f,0.00001f,0.00003f,0.0001f,0.0003f,
                    0.001f,0.002f,0.003f,0.005f,0.01f,0.02f,0.05f})
        result.push_back({Method::constant,b});
    for (float m : {0.05f,0.1f,0.2f,0.3f,0.5f,0.65f,0.85f,1.0f,1.5f})
        result.push_back({Method::slope,m});
    for (float b : {0.0000005f,0.000002f,0.00001f,0.00003f,0.0001f,0.0003f,0.001f,0.003f})
        result.push_back({Method::receiver,b});
    for (auto m : {Method::numeric,Method::footprint,Method::full})
        for (float s : {1.0f,2.0f,3.0f}) result.push_back({m,s});
    return result;
}

std::ofstream output_file(const fs::path& path) {
    std::ofstream out(path,std::ios::binary);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    return out;
}

void csv_header(std::ostream& output) {
    output << "case,scene,view,sun_deg,azimuth_deg,map_size,warp,pcf_radius,width,height,method,parameter,ring,"
           << "lit,shadow,false_shadow,missed_shadow,contact,missed_contact\n";
}

StudyScores report_case(const Config& cfg, const Frame& frame, const ShadowMap& map, std::ostream& rows,
                        const std::vector<Variant>& variants) {
    std::vector<std::array<Counts,4>> counts(variants.size());
    for (const Receiver& pixel : frame.pixels) {
        if (pixel.category < 2) continue;
        const Features f = sample_features(map,pixel,cfg.radius);
        const float r = std::max(std::abs(pixel.p.x),std::abs(pixel.p.y));
        const int ring = r < 1.0f/3.0f ? 1 : r < 2.0f/3.0f ? 2 : 3;
        const int midpoint = f.tap_count/2;
        for (std::size_t i = 0; i < variants.size(); ++i) {
            const auto [method,value] = variants[i];
            const bool shadow = shadow_count(f,method,value) > midpoint;
            counts[i][0].add(pixel.category,shadow);
            counts[i][ring].add(pixel.category,shadow);
        }
    }
    StudyScores defaults{};
    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto method = variants[i].method;
        const auto index = static_cast<std::size_t>(method);
        if (variants[i].value == default_params[index]) {
            for (std::size_t n = 0; n < study_preview_methods.size(); ++n) {
                if (study_preview_methods[n] == method) defaults[n] = counts[i][0];
            }
        }
        constexpr std::array rings{"all","inner","middle","outer"};
        for (std::size_t r = 0; r < rings.size(); ++r) {
            rows << cfg.id() << ',' << cfg.scene << ',' << (cfg.contacts ? "contact" : "overview")
                << ',' << cfg.sun << ',' << cfg.azimuth << ',' << cfg.size << ',' << cfg.alpha
                << ',' << cfg.radius << ',' << frame.width << ',' << frame.height
                << ',' << method_names[static_cast<int>(variants[i].method)]
                << ',' << std::setprecision(7) << variants[i].value << ',' << rings[r];
            counts[i][r].write(rows);
            rows << '\n';
        }
    }
    return defaults;
}

template<typename T>
void save_binary(const fs::path& path, const std::vector<T>& data) {
    static_assert(std::endian::native == std::endian::little, "Fixture byte order is little endian.");
    auto file = output_file(path);
    file.write(reinterpret_cast<const char*>(data.data()),data.size()*sizeof(T));
}

void write_ppm(const fs::path& path, const Frame& frame, const std::vector<std::uint8_t>& bytes) {
    auto file = output_file(path);
    file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    const int stride = frame.width*3;
    for (int y = frame.height-1; y >= 0; --y)
        file.write(reinterpret_cast<const char*>(bytes.data()+y*stride),stride);
}

void write_pgm(const fs::path& path, const Frame& frame, const std::vector<std::uint8_t>& bytes) {
    auto file = output_file(path);
    file << "P5\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (int y = frame.height-1; y >= 0; --y)
        file.write(reinterpret_cast<const char*>(bytes.data()+y*frame.width),frame.width);
}

void capture(const fs::path& dir, const Config& cfg, const Frame& frame, const ShadowMap& map, bool raw) {
    if (!dir.empty()) fs::create_directories(dir);
    const auto total = frame.pixels.size();
    std::array<std::vector<std::uint8_t>,method_names.size()> masks;
    for (auto& mask : masks) mask.resize(total);
    for (std::size_t i = 0; i < total; ++i) {
        const auto& r = frame.pixels[i];
        if (r.category < 2) continue;
        const Features f = sample_features(map,r,cfg.radius);
        for (int m = 0; m < static_cast<int>(Method::reference); ++m) {
            const int n = shadow_count(f,static_cast<Method>(m),default_params[m]);
            masks[m][i] = static_cast<std::uint8_t>(std::lround(255.0*n/f.tap_count));
        }
        masks[static_cast<int>(Method::reference)][i] = r.category >= 3 ? 255 : 0;
    }
    for (int m = 0; m < static_cast<int>(masks.size()); ++m) {
        const std::string prefix = method_names[m];
        if (raw) {
            save_binary(dir/("hard_"+prefix+".bin"),masks[m]);
            continue;
        }
        std::vector<std::uint8_t> shaded(total*3), errors(total*3);
        for (std::size_t i = 0; i < total; ++i) {
            const auto& r = frame.pixels[i];
            const bool false_shadow = r.category == 2 && masks[m][i] >= 128;
            const bool miss = r.category >= 3 && masks[m][i] < 128;
            const std::uint8_t gray = (r.rgb[0]+r.rgb[1]+r.rgb[2])/11;
            const std::array<std::uint8_t,3> overlay = false_shadow ? std::array<std::uint8_t,3>{255,105,72}
                : miss ? std::array<std::uint8_t,3>{86,211,238} : std::array<std::uint8_t,3>{gray,gray,gray};
            for (int k = 0; k < 3; ++k) {
                shaded[i*3+k] = static_cast<std::uint8_t>(std::lround(r.rgb[k]*(1.0-0.69*masks[m][i]/255.0)));
                errors[i*3+k] = overlay[k];
            }
        }
        write_ppm(dir/(prefix+"_shaded.ppm"),frame,shaded);
        write_ppm(dir/(prefix+"_errors.ppm"),frame,errors);
        write_pgm(dir/(prefix+"_mask.pgm"),frame,masks[m]);
    }
    if (!raw) {
        std::vector<std::uint8_t> categories(total);
        for (std::size_t i = 0; i < total; ++i) categories[i] = frame.pixels[i].category;
        write_pgm(dir/"classes.pgm",frame,categories);
    }
}

void fixture(const fs::path& dir, const Config& cfg, const Frame& frame, const ShadowMap& map) {
    fs::create_directories(dir);
    const auto data = pack_inputs(frame,map);
    save_binary(dir/"positions.bin",data.positions);
    save_binary(dir/"gradients.bin",data.gradients);
    save_binary(dir/"input_error.bin",data.error);
    save_binary(dir/"base.bin",data.base);
    save_binary(dir/"depth_slope.bin",data.texels);
    save_binary(dir/"caster_planes.bin",data.planes);
    const int original_radius = cfg.radius;
    auto hard = cfg;
    hard.radius = 0;
    capture(dir,hard,frame,map,true);
    auto meta = output_file(dir/"fixture.json");
    meta << "{\"schema\":1,\"id\":\"" << cfg.id() << "\",\"scene\":\"" << cfg.scene
        << "\",\"view\":\"" << (cfg.contacts ? "contact" : "overview") << "\",\"sun\":" << cfg.sun
        << ",\"azimuth\":" << cfg.azimuth << ",\"size\":" << cfg.size << ",\"warp\":" << cfg.alpha
        << ",\"width\":" << frame.width << ",\"height\":" << frame.height
        << ",\"pcf_radius\":" << original_radius << "}\n";
}

bool null_tests(const fs::path& dest, const Heightmap& map, int jobs, std::stop_token stop,
                const CaseProgress& progress) {
    const Bvh mesh = make_mesh(map,false);
    const Light light(18,37);
    const Frame ref = make_frame(mesh,light,280,210,false,jobs);
    for (const auto& p : ref.pixels) {
        if (p.category > 2) throw std::runtime_error("Same-plane null: reference contains a shadow.");
    }
    auto out = output_file(dest/"null.csv");
    csv_header(out);
    for (int alpha : {0,8,16}) {
        if (stop.stop_requested()) return false;
        Config cfg{"ramp",18,37,256,alpha,0,false,true};
        if (progress) progress({0, "Same-plane null test", cfg.preview(), std::nullopt});
        const ShadowMap sm(mesh,light,256,alpha,jobs);
        const std::vector<Variant> methods{{Method::constant,0.0001f},{Method::slope,0.5f},
            {Method::receiver,0.000002f},{Method::numeric,2.0f},{Method::footprint,2.0f},{Method::full,2.0f}};
        report_case(cfg,ref,sm,out,methods);
        for (const auto& p : ref.pixels) {
            if (p.category != 2) continue;
            const auto f = sample_features(sm,p,0);
            if (shadow_count(f,Method::numeric,2.0f) || shadow_count(f,Method::footprint,2.0f)
                || shadow_count(f,Method::full,2.0f)) throw std::runtime_error("Same-plane null failed.");
        }
    }
    for (int alpha : {0,8,16}) for (double x : {-1.0,-0.99,-0.87,-0.3,0.0,0.22,0.96,1.0}) {
        if (std::abs(unwarp(warp(x,alpha),alpha)-x) > 1e-12) throw std::runtime_error("Warp inverse failed.");
    }
    return true;
}

void help() {
    std::cout << R"(gfx-research-shadow-auto-bias
  shadow-auto-bias-benchmark --out runs/pilot [--profile pilot|production|full] [--fixtures viewer/cases] [--pcf]
  shadow-auto-bias-benchmark --scene ridge --out runs/ridge [--sun 18] [--azimuth 37] [--map 512] [--warp 8]
              [--contact] [--fixture viewer/cases/my-case] [--heightmap my-terrain.pgm]
              [--width 1920 --height 1080]
  shadow-auto-bias-benchmark --generate-heightmaps
  Default output is a new run under results/benchmarks beside the executable.
  --heightmaps DIR defaults to the bundled heightmaps. --jobs N sets worker threads.
  Main study uses Hard. --pcf enables a fixed equal-weight 3 x 3 PCF kernel.
)";
}

struct Options {
    fs::path heightmaps, out, custom, fixture_dir, fixtures;
    std::string profile = "pilot";
    Config cfg{};
    int width = image_width, height = image_height;
    int jobs = static_cast<int>(std::clamp(std::thread::hardware_concurrency(),1u,8u));
    bool generate = false;
};

int integer(std::string value, int minimum, int maximum, std::string_view option) {
    std::size_t read;
    const int n = std::stoi(value,&read);
    if (read != value.size() || n < minimum || n > maximum)
        throw std::runtime_error("Invalid value for "+std::string(option));
    return n;
}

Options options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        auto value = [&]() {
            if (++i >= argc) throw std::runtime_error("Missing value for "+std::string(flag));
            return std::string(argv[i]);
        };
        if (flag == "--profile") o.profile = value();
        else if (flag == "--out") o.out = value();
        else if (flag == "--heightmaps") o.heightmaps = value();
        else if (flag == "--heightmap") o.custom = value();
        else if (flag == "--scene") o.cfg.scene = value();
        else if (flag == "--fixtures") o.fixtures = value();
        else if (flag == "--fixture") o.fixture_dir = value();
        else if (flag == "--width") o.width = integer(value(),64,4096,flag);
        else if (flag == "--height") o.height = integer(value(),64,4096,flag);
        else if (flag == "--jobs") o.jobs = integer(value(),1,32,flag);
        else if (flag == "--sun") o.cfg.sun = integer(value(),5,85,flag);
        else if (flag == "--azimuth") o.cfg.azimuth = integer(value(),0,359,flag);
        else if (flag == "--map") o.cfg.size = integer(value(),64,2048,flag);
        else if (flag == "--warp") o.cfg.alpha = integer(value(),0,24,flag);
        else if (flag == "--pcf") o.cfg.radius = 1;
        else if (flag == "--contact") o.cfg.contacts = true;
        else if (flag == "--generate-heightmaps") o.generate = true;
        else throw std::runtime_error("Unknown option: "+std::string(flag));
    }
    if (o.profile != "pilot" && o.profile != "production" && o.profile != "full")
        throw std::runtime_error("Choose pilot, production, or full.");
    if (!o.custom.empty()) o.cfg.scene = "custom";
    if (!o.cfg.scene.empty() && o.cfg.scene != "custom"
            && std::find(scene_names.begin(),scene_names.end(),o.cfg.scene) == scene_names.end())
        throw std::runtime_error("Choose ramp, hills, terraces, ridge, or supply --heightmap.");
    if (o.cfg.scene == "custom" && o.custom.empty()) throw std::runtime_error("Supply --heightmap for custom terrain.");
    if (o.cfg.scene.empty() && !o.fixture_dir.empty()) throw std::runtime_error("--fixture requires --scene or --heightmap.");
    return o;
}

struct StudyGrid {
    std::vector<int> elevations, azimuths, resolutions, warps;

    int cases() const {
        return static_cast<int>(scene_names.size() * elevations.size() * azimuths.size()
            * resolutions.size() * warps.size());
    }
};

StudyGrid grid_for(std::string_view profile) {
    if (profile == "full") return {{12,18,38,58},{37,113},{256,512,1024,2048},{0,4,8,16}};
    if (profile == "production") return {{18,38},{37},{2048},{8,16}};
    return {{18,38},{37},{256,512},{0,8}};
}

const char* profile_name(StudyProfile profile) {
    return profile == StudyProfile::pilot ? "pilot" : "production";
}

bool run(const Options& o, std::stop_token stop = {}, const CaseProgress& progress = {}) {
    fs::create_directories(o.out);
    auto rows = output_file(o.out/"quality.csv");
    csv_header(rows);
    auto manifest = output_file(o.out/"manifest.txt");
    manifest << "program=gfx-research-shadow-auto-bias\nversion=" << version << "\nprofile="
        << (o.cfg.scene.empty() ? o.profile : "single") << "\nframe_width=" << o.width
        << "\nframe_height=" << o.height << "\ndepth_bits=24\nlight_half_width_m=128\n"
        << "light_depth_range_m=512\nwarp_center=0,0\nwarp_offset=0,0\n"
        << "alpha_depth_warp=none\nnormal_encoding=octahedral_snorm16\n"
        << "reference=double_bvh_ray_triangle\nray_epsilon_m=1e-6\ncontact_band_m=2\n"
        << "minimum_facing_cosine=0.05\nfootprint_weight=0.9\nauto_bias_scale=2\npcf_radius="
        << o.cfg.radius << "\ncompiler=" <<
#ifdef _MSC_VER
            "MSVC"
#else
            __VERSION__
#endif
        << "\nworkers=" << o.jobs << '\n';
    const auto finalize = [&](int cases, bool complete) {
        manifest << "cases=" << cases << "\ncompleted=" << (complete ? 1 : 0) << '\n';
        rows.flush();
        manifest.flush();
        if (progress) progress({cases, complete ? "All cases scored" : "Stopped", {}, {}});
        return complete;
    };
    if (stop.stop_requested()) return finalize(0, false);
    const std::vector<Variant> variants = sweep();
    if (!o.cfg.scene.empty()) {
        const auto h = load_heightmap(o.custom.empty() ? o.heightmaps/(o.cfg.scene+".pgm") : o.custom);
        const Bvh mesh = make_mesh(h,true);
        const Config& c = o.cfg;
        const Light light(c.sun,c.azimuth);
        const Frame frame = make_frame(mesh,light,o.width,o.height,c.contacts,o.jobs);
        const ShadowMap map(mesh,light,c.size,c.alpha,o.jobs);
        report_case(c,frame,map,rows,variants);
        capture(o.out/"captures"/c.id(),c,frame,map,false);
        if (!o.fixture_dir.empty()) fixture(o.fixture_dir,c,frame,map);
        manifest << "scene=" << c.scene << '\n';
        return finalize(1, true);
    }
    if (progress) progress({0, "Preparing same-plane null tests", {}, {}});
    if (!null_tests(o.out, load_heightmap(o.heightmaps / "ramp.pgm"), o.jobs, stop, progress))
        return finalize(0, false);
    const auto [suns, azimuths, sizes, strengths] = grid_for(o.profile);
    int cases = 0;
    for (const std::string scene : scene_names) {
        if (stop.stop_requested()) return finalize(cases, false);
        const auto h = load_heightmap(o.heightmaps/(scene+".pgm"));
        const Bvh mesh = make_mesh(h,true);
        for (int sun : suns) for (int az : azimuths) {
            if (stop.stop_requested()) return finalize(cases, false);
            const Light light(sun,az);
            const Frame frame = make_frame(mesh,light,o.width,o.height,false,o.jobs);
            for (int size : sizes) for (int alpha : strengths) {
                if (stop.stop_requested()) return finalize(cases, false);
                const Config c{scene,sun,az,size,alpha,o.cfg.radius};
                const auto id = c.id();
                if (progress) progress({cases, id, c.preview(), {}});
                const ShadowMap map(mesh,light,size,alpha,o.jobs);
                const auto scores = report_case(c,frame,map,rows,variants);
                ++cases;
                if (sun == 18 && az == 37 && size == 512 && alpha == 8) {
                    capture(o.out/"captures"/c.id(),c,frame,map,false);
                    if (!o.fixtures.empty()) fixture(o.fixtures/c.id(),c,frame,map);
                    if (scene == "ridge" || scene == "terraces") {
                        auto close = c;
                        close.contacts = true;
                        const auto contact = make_frame(mesh,light,o.width,o.height,true,o.jobs);
                        capture(o.out/"captures"/close.id(),close,contact,map,false);
                        if (!o.fixtures.empty()) fixture(o.fixtures/close.id(),close,contact,map);
                    }
                }
                if (!o.fixtures.empty() && scene == "ridge" && sun == 18 && az == 37
                        && ((size == 512 && alpha == 0) || (size == 256 && alpha == 8))) {
                    fixture(o.fixtures/c.id(),c,frame,map);
                }
                if (progress) progress({cases, id, c.preview(), scores});
            }
        }
        if (!progress)
            std::cout << "Completed " << scene << " (" << cases << " scored cases)\n" << std::flush;
    }
    return finalize(cases, true);
}

}  // namespace

int lab::study_case_count(StudyProfile profile) {
    return grid_for(profile_name(profile)).cases();
}

bool lab::run_study(StudyProfile profile, const fs::path& heightmaps, const fs::path& output,
                    std::stop_token stop, const CaseProgress& progress) {
    Options options;
    options.heightmaps = heightmaps;
    options.out = output;
    options.profile = profile_name(profile);
    return run(options, stop, progress);
}

int benchmark_main(int argc, char** argv) {
    try {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--help" || std::string_view(argv[i]) == "-h") {
                help();
                return 0;
            }
        }
        Options o = options(argc,argv);
        if (o.heightmaps.empty()) {
            o.heightmaps = o.generate || !o.custom.empty()
                ? fs::path("heightmaps") : lab::asset_root(argv[0],"heightmaps/ramp.pgm")/"heightmaps";
        }
        if (o.generate) {
            lab::write_heightmaps(o.heightmaps);
        } else {
            if (o.out.empty()) {
                const auto name = o.cfg.scene.empty() ? o.profile : o.cfg.scene;
                o.out = lab::create_run_directory(lab::default_results_directory(argv[0]) / "benchmarks", name);
            }
            run(o);
            std::cout << "Results saved in " << fs::absolute(o.out).string() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "shadow-auto-bias-benchmark: " << error.what() << '\n';
        return 1;
    }
}
