// Lighting maps: normal maps from a picture's outline and brightness, cell by cell, and glow
// maps of its brightest colours.

#include "engine/assets/lighting_maps.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace assets = engine::assets;

namespace {

/// A `width` x `height` picture, clear, with an opaque grey rectangle from (x0, y0) to
/// (x1, y1), exclusive.
assets::Image picture(std::uint32_t width, std::uint32_t height, std::uint32_t x0, std::uint32_t y0,
                      std::uint32_t x1, std::uint32_t y1) {
    assets::Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(std::size_t{width} * height * 4, 0);
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const std::size_t i = ((std::size_t{y} * width) + x) * 4;
            image.rgba[i] = 100;
            image.rgba[i + 1] = 100;
            image.rgba[i + 2] = 100;
            image.rgba[i + 3] = 255;
        }
    }
    return image;
}

std::array<int, 4> at(const assets::Image& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = ((std::size_t{y} * image.width) + x) * 4;
    return {image.rgba[i], image.rgba[i + 1], image.rgba[i + 2], image.rgba[i + 3]};
}

void set(assets::Image& image, std::uint32_t x, std::uint32_t y, std::array<std::uint8_t, 4> c) {
    const std::size_t i = ((std::size_t{y} * image.width) + x) * 4;
    for (std::size_t k = 0; k < 4; ++k) {
        image.rgba[i + k] = c.at(k);
    }
}

constexpr std::array<int, 4> flat{128, 128, 255, 255};

} // namespace

TEST_CASE("Normal maps: rounded edges facing out, a flat top, flat where clear") {
    // A 10 x 10 square in a 16 x 16 picture, rounded over 2 pixels.
    const assets::Image square = picture(16, 16, 3, 3, 13, 13);
    assets::NormalMapSettings settings;
    settings.bevel = 2;
    const assets::Image normals = assets::make_normal_map(square, settings);
    REQUIRE(normals.width == 16);
    REQUIRE(normals.height == 16);
    CHECK(at(normals, 8, 8) == flat); // the top
    CHECK(at(normals, 0, 0) == flat); // clear
    // The left edge faces left, the right edge right, the top edge up the picture (green
    // above 128) and the bottom edge down; each still faces out of it.
    CHECK(at(normals, 3, 8)[0] < 64);
    CHECK(at(normals, 12, 8)[0] > 192);
    CHECK(at(normals, 8, 3)[1] > 192);
    CHECK(at(normals, 8, 12)[1] < 64);
    CHECK(at(normals, 3, 8)[2] > 128);
    CHECK(at(normals, 3, 8)[1] == 128); // halfway down the left edge: no tilt up or down
    // Mirrored edges mirror each other.
    CHECK(at(normals, 3, 8)[0] + at(normals, 12, 8)[0] == 255);
    // Every pixel opaque, so the map is never drawn see-through.
    for (std::size_t i = 3; i < normals.rgba.size(); i += 4) {
        REQUIRE(normals.rgba[i] == 255);
    }
    // Stronger: steeper.
    settings.strength = 3.0F;
    CHECK(assets::make_normal_map(square, settings).rgba[0] == 128);
    CHECK(at(assets::make_normal_map(square, settings), 3, 8)[0] < at(normals, 3, 8)[0]);
    // Strength 0: flat everywhere.
    settings.strength = 0.0F;
    const assets::Image none = assets::make_normal_map(square, settings);
    CHECK(at(none, 3, 8) == flat);
    // A broken picture gives nothing.
    assets::Image broken = square;
    broken.rgba.pop_back();
    CHECK(assets::make_normal_map(broken, settings).width == 0);
}

TEST_CASE("Normal maps: bumps from brightness, and cells kept apart") {
    // A dark opaque strip with one bright pixel in it: bumpy raises it.
    assets::Image strip = picture(5, 1, 0, 0, 5, 1);
    set(strip, 2, 0, {250, 250, 250, 255});
    assets::NormalMapSettings bumpy;
    bumpy.relief = assets::Relief::Bumpy;
    const assets::Image bumps = assets::make_normal_map(strip, bumpy);
    CHECK(at(bumps, 1, 0)[0] < 128);  // rising to the right: tilted left
    CHECK(at(bumps, 3, 0)[0] > 128);  // falling to the right: tilted right
    CHECK(at(bumps, 2, 0)[0] == 128); // the top of the bump
    CHECK(at(bumps, 0, 0)[0] == at(bumps, 4, 0)[0]);

    // Two 4 x 4 opaque tiles side by side: as one picture, the seam between them is flat;
    // as cells of 4, each rounds off at its own edges.
    const assets::Image tiles = picture(8, 4, 0, 0, 8, 4);
    assets::NormalMapSettings rounded;
    rounded.bevel = 1;
    const assets::Image whole = assets::make_normal_map(tiles, rounded);
    CHECK(at(whole, 3, 2)[0] == 128);
    CHECK(at(whole, 4, 2)[0] == 128);
    rounded.cell_width = 4;
    rounded.cell_height = 4;
    const assets::Image cells = assets::make_normal_map(tiles, rounded);
    CHECK(at(cells, 3, 2)[0] > 128); // the first tile's right edge faces right
    CHECK(at(cells, 4, 2)[0] < 128); // the second's left edge faces left
    // Both: the sum of the two.
    assets::NormalMapSettings both = rounded;
    both.relief = assets::Relief::Both;
    CHECK(assets::make_normal_map(tiles, both).width == 8);
}

TEST_CASE("Glow maps: the brightest colours, opaque, and nothing else") {
    assets::Image image = picture(3, 1, 0, 0, 3, 1);
    set(image, 1, 0, {255, 200, 40, 255}); // a lit window
    set(image, 2, 0, {255, 255, 255, 0});  // bright but clear
    const assets::Image glow = assets::make_glow_map(image, 0.8F);
    REQUIRE(glow.width == 3);
    CHECK(at(glow, 0, 0) == std::array<int, 4>{0, 0, 0, 0});
    CHECK(at(glow, 1, 0) == std::array<int, 4>{255, 200, 40, 255});
    CHECK(at(glow, 2, 0) == std::array<int, 4>{0, 0, 0, 0});
    // Threshold 0: every opaque pixel glows.
    CHECK(at(assets::make_glow_map(image, 0.0F), 0, 0) == std::array<int, 4>{100, 100, 100, 255});
}
