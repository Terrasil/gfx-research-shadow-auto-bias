#include "publication_figure.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lab {
namespace {
namespace fs = std::filesystem;

struct Image {
    int width, height;
    std::vector<std::uint8_t> rgb;
};

Image read_capture(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    int width = 0, height = 0, max_value = 0;
    input >> magic >> width >> height >> max_value;
    if (magic != "P6" || max_value != 255 || input.get() != '\n'
            || width < 1 || width > 4096 || height < 1 || height > 4096)
        throw std::runtime_error("Invalid benchmark capture: " + path.string());
    Image image{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 3)};
    if (!input.read(reinterpret_cast<char*>(image.rgb.data()), static_cast<std::streamsize>(image.rgb.size())))
        throw std::runtime_error("Truncated benchmark capture: " + path.string());
    return image;
}

void write_png(const fs::path& path, const Image& image) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write " + path.string());
    const auto write = [](void* context, void* bytes, int length) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(bytes), length);
    };
    const int encoded = stbi_write_png_to_func(write, &output, image.width, image.height, 3,
        image.rgb.data(), image.width * 3);
    output.close();
    if (!encoded || !output) {
        std::error_code ignored;
        fs::remove(path, ignored);
        throw std::runtime_error("Cannot encode " + path.string());
    }
}

void blit(Image& grid, const Image& tile, int x, int y) {
    for (int row = 0; row < tile.height; ++row) {
        const auto source = tile.rgb.data() + static_cast<std::size_t>(row) * tile.width * 3;
        auto dest = grid.rgb.data() + (static_cast<std::size_t>(y + row) * grid.width + x) * 3;
        std::copy_n(source, tile.width * 3, dest);
    }
}

void title(Image& image, std::string_view text, int x, int y, int width) {
    // Five by seven bitmap capitals for the four captions.
    constexpr std::string_view letters = "ACDEFILNOPRSTUY()";
    constexpr std::uint8_t glyphs[][7]{
        {14,17,17,31,17,17,17}, {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,4,4,4,4,4,14},
        {16,16,16,16,16,16,31}, {17,25,25,21,19,19,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},
        {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,10,4,4,4,4},
        {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8}
    };
    constexpr int scale = 3, advance = 6 * scale;
    const int label_width = static_cast<int>(text.size()) * advance - scale;
    if (label_width > width) throw std::runtime_error("Publication caption exceeds the panel width.");
    x += (width - label_width) / 2;
    for (const char c : text) {
        const auto glyph = letters.find(c);
        if (glyph != std::string_view::npos) {
            for (int row = 0; row < 7 * scale; ++row) {
                for (int col = 0; col < 5 * scale; ++col) {
                    if ((glyphs[glyph][row / scale] & (16u >> (col / scale))) == 0) continue;
                    auto dest = image.rgb.data() + (static_cast<std::size_t>(y + row) * image.width + x + col) * 3;
                    std::fill_n(dest, 3, 25);
                }
            }
        }
        x += advance;
    }
}

void make_figure(const fs::path& capture) {
    constexpr std::array files{"constant", "footprint", "full", "reference"};
    constexpr std::array labels{"CONSTANT (DEFAULT)", "FOOTPRINT", "AUTO", "RAY REFERENCE"};
    constexpr int gap = 6, header = 40;
    const auto first = read_capture(capture / "constant_shaded.ppm");
    const int width = first.width * static_cast<int>(files.size()) + gap * 5;
    const int height = first.height * 2 + header + gap * 2;
    Image grid{width, height, std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 3, 255)};

    for (int col = 0; col < static_cast<int>(files.size()); ++col) {
        const int x = gap + col * (first.width + gap);
        title(grid, labels[col], x, (header - 21) / 2, first.width);
        for (int row = 0; row < 2; ++row) {
            const auto name = std::string(files[col]) + (row == 0 ? "_shaded.ppm" : "_errors.ppm");
            const auto tile = row == 0 && col == 0 ? first : read_capture(capture / name);
            if (tile.width != first.width || tile.height != first.height)
                throw std::runtime_error("Mismatched capture dimensions in " + capture.string());
            blit(grid, tile, x, header + row * (first.height + gap));
        }
    }
    write_png(capture / "comparison.png", grid);
}

}  // namespace

void write_publication_figures(const fs::path& pilot, std::stop_token stop) {
    for (const auto& entry : fs::directory_iterator(pilot / "captures")) {
        if (stop.stop_requested()) return;
        if (entry.is_directory()) make_figure(entry.path());
    }
}

}  // namespace lab
