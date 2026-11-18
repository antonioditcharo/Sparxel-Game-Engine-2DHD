// The character generator: sheets drawn from choices and colours, and placed to walk.

#include "engine/assets/assets.h"
#include "engine/foundation/guid.h"
#include "engine/game/behaviours.h"
#include "engine/scene2d/scene2d.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "editor/core/characters.h"
#include "editor/core/document.h"
#include "editor/core/new_project.h"

namespace fs = std::filesystem;
namespace game = engine::game;
namespace scene2d = engine::scene2d;
using Rgb = editor::CharacterLook::Rgb;

namespace {

/// The colour at (x, y), or nothing where it is clear.
std::optional<Rgb> at(const engine::assets::Image& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = ((std::size_t{y} * image.width) + x) * 4;
    if (image.rgba.at(i + 3) == 0) {
        return std::nullopt;
    }
    return Rgb{image.rgba.at(i), image.rgba.at(i + 1), image.rgba.at(i + 2)};
}

} // namespace

TEST_CASE("Characters: four rows of three frames, each part in its colour") {
    editor::CharacterLook look;
    const engine::assets::Image sheet = editor::character_sheet(look);
    REQUIRE(sheet.width == 48);
    REQUIRE(sheet.height == 64);
    // A picture to look at, eight times the size, in the temporary folder.
    {
        const fs::path out = fs::temp_directory_path() / "sparxel-character-sheet.png";
        engine::assets::Image big;
        big.width = sheet.width * 8;
        big.height = sheet.height * 8;
        big.rgba.resize(std::size_t{big.width} * big.height * 4);
        for (std::uint32_t y = 0; y < big.height; ++y) {
            for (std::uint32_t x = 0; x < big.width; ++x) {
                for (std::size_t c = 0; c < 4; ++c) {
                    big.rgba[((std::size_t{y} * big.width) + x) * 4 + c] =
                        sheet.rgba[((std::size_t{y / 8} * sheet.width) + x / 8) * 4 + c];
                }
            }
        }
        const std::vector<std::uint8_t> png = engine::assets::encode_png(big);
        std::ofstream(out, std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    constexpr Rgb eye{24, 22, 34};
    // Down, standing (the middle frame): two eyes, the top, both shoes on the bottom row.
    CHECK(at(sheet, 16 + 6, 5) == eye);
    CHECK(at(sheet, 16 + 9, 5) == eye);
    CHECK(at(sheet, 16 + 7, 9) == look.top);
    CHECK(at(sheet, 16 + 5, 15) == look.shoes);
    CHECK(at(sheet, 16 + 9, 15) == look.shoes);
    CHECK_FALSE(at(sheet, 16 + 0, 0).has_value()); // clear around the figure
    // A step lifts a foot: the left one in the first frame, the right one in the third.
    CHECK_FALSE(at(sheet, 5, 15).has_value());
    CHECK(at(sheet, 5, 14) == look.shoes);
    CHECK(at(sheet, 9, 15) == look.shoes);
    CHECK_FALSE(at(sheet, 32 + 9, 15).has_value());
    // Left: one eye at the front; right: the same, mirrored.
    CHECK(at(sheet, 16 + 6, 16 + 5) == eye);
    CHECK(at(sheet, 16 + 9, 32 + 5) == eye);
    CHECK(at(sheet, 16 + 9, 16 + 5) != eye);
    // Up: the back of the head is hair, no eyes.
    CHECK(at(sheet, 16 + 6, 48 + 5) == look.hair_color);
    CHECK(at(sheet, 16 + 9, 48 + 5) == look.hair_color);
    // Choices change it: no hair shows skin on top; a hat its colour over the head.
    look.hair = editor::CharacterLook::Hair::None;
    CHECK(at(editor::character_sheet(look), 16 + 7, 3) == look.skin);
    CHECK_FALSE(at(editor::character_sheet(look), 16 + 7, 1).has_value());
    look.headwear = editor::CharacterLook::Headwear::Hat;
    CHECK(at(editor::character_sheet(look), 16 + 7, 1) == look.headwear_color);
    look.hair = editor::CharacterLook::Hair::Long;
    look.headwear = editor::CharacterLook::Headwear::None;
    CHECK(at(editor::character_sheet(look), 16 + 4, 8) == look.hair_color); // down the sides
    CHECK(editor::parse_rgb("#ff8000") == Rgb{255, 128, 0});
    CHECK_FALSE(editor::parse_rgb("orange").has_value());
}

TEST_CASE("Characters are placed standing, cut into frames, ready to walk, as one step") {
    const fs::path folder =
        fs::temp_directory_path() / ("sparxel-character-" + engine::Guid::generate().to_string());
    std::string error;
    REQUIRE(editor::create_project(folder, "People", error));
    {
        const std::vector<std::uint8_t> png =
            engine::assets::encode_png(editor::character_sheet(editor::CharacterLook{}));
        std::ofstream(folder / "assets" / "art" / "villager.png", std::ios::binary)
            .write(reinterpret_cast<const char*>(png.data()), // NOLINT
                   static_cast<std::streamsize>(png.size()));
    }
    std::vector<engine::serialize::Issue> issues;
    const auto d = editor::Document::open(folder, issues);
    REQUIRE(d != nullptr);
    static_cast<void>(d->assets().refresh());
    const std::size_t before = d->entities().size();
    const engine::Guid placed = d->place_character("art/villager.png", 4.3, 5.0, error);
    REQUIRE(placed.is_valid());
    const engine::world::Entity e = d->world().find(placed);
    const scene2d::Sprite* sprite = d->world().get<scene2d::Sprite>(e);
    REQUIRE(sprite != nullptr);
    CHECK(sprite->frame_width == 16);
    CHECK(sprite->frame_height == 16);
    CHECK(sprite->frame == 1);
    CHECK(d->world().get<game::WalkAnimation>(e) != nullptr);
    CHECK(d->world().get<game::TopDownMover>(e) != nullptr);
    REQUIRE(d->world().get<game::Walker>(e) != nullptr);
    CHECK(d->world().get<game::Walker>(e)->mode == game::WalkMode::Stand);
    CHECK(d->world().name(e) == "villager");
    CHECK(d->world().get<scene2d::Transform2D>(e)->x == doctest::Approx(4.3125)); // a pixel
    CHECK_FALSE(d->place_character("art/nothing.png", 0, 0, error).is_valid());
    REQUIRE(d->undo());
    CHECK(d->entities().size() == before);
    std::error_code ignored;
    fs::remove_all(folder, ignored);
}
