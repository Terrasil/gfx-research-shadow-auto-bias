#include "../src/shadow.hpp"

#include <iostream>

int main() {
    using namespace lab;
    for (int a : {0,8,16,32}) {
        for (double p : {-1.0,-0.999,-0.5,0.0,0.4,0.999,1.0}) {
            if (std::abs(unwarp(warp(p,a),a)-p)>1e-12) {
                std::cerr << "Warp round-trip failed.\n";
                return 1;
            }
        }
    }
    const Heightmap flat{2,2,{0.0,0.0,0.0,0.0}};
    const Bvh mesh = make_mesh(flat,false);
    const Light light(18,37);
    const Frame frame = make_frame(mesh,light,96,72,false,1);
    const ShadowMap shadow(mesh,light,128,8,1);
    std::size_t count=0;
    for (const auto& receiver : frame.pixels) {
        if (receiver.category<2) continue;
        ++count;
        if (receiver.category!=2 || shadow_count(sample_features(shadow,receiver,0),Method::full,2.0f)!=0) {
            std::cerr << "Auto failed the same-plane null test.\n";
            return 1;
        }
    }
    return count==0 ? 1 : 0;
}
