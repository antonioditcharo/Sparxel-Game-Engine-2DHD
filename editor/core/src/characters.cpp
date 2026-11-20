#include "editor/core/characters.h"

#include <charconv>
#include <optional>
#include <system_error>

namespace editor {

namespace {

using Rgb = CharacterLook::Rgb;
using Hair = CharacterLook::Hair;
using Headwear = CharacterLook::Headwear;

constexpr Rgb eye{24, 22, 34};

Rgb darker(const Rgb& c) {
    return {static_cast<std::uint8_t>(c[0] * 3 / 4), static_cast<std::uint8_t>(c[1] * 3 / 4),
            static_cast<std::uint8_t>(c[2] * 3 / 4)};
}

enum class Facing : std::uint8_t { Down, Left, Right, Up };

/// One frame being drawn: at (left, top) of the sheet, mirrored for facing right.
struct Frame {
    engine::assets::Image& image;
    std::uint32_t left;
    std::uint32_t top;
    bool mirror;

    void put(int x, int y, const Rgb& c) const {
        if (x < 0 || y < 0 || x >= static_cast<int>(character_frame) ||
            y >= static_cast<int>(character_frame)) {
            return;
        }
        const auto fx =
            static_cast<std::uint32_t>(mirror ? static_cast<int>(character_frame) - 1 - x : x);
        const std::size_t at =
            ((std::size_t{top + static_cast<std::uint32_t>(y)} * image.width) + left + fx) * 4;
        image.rgba[at] = c[0];
        image.rgba[at + 1] = c[1];
        image.rgba[at + 2] = c[2];
        image.rgba[at + 3] = 255;
    }
    void box(int x0, int y0, int x1, int y1, const Rgb& c) const {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                put(x, y, c);
            }
        }
    }
};

/// The legs and shoes: `step` 0 and 2 are the two steps, 1 standing.
void legs(const Frame& f, const CharacterLook& look, Facing facing, int step) {
    if (facing == Facing::Down || facing == Facing::Up) {
        // Two legs side by side; a step lifts one foot a pixel.
        for (const int leg : {0, 1}) {
            const int x = leg == 0 ? 5 : 9;
            const bool lifted = (step == 0 && leg == 0) || (step == 2 && leg == 1);
            f.box(x, 13, x + 1, lifted ? 13 : 14, look.bottom);
            f.box(x, lifted ? 14 : 15, x + 1, lifted ? 14 : 15, look.shoes);
        }
        return;
    }
    // Seen from the side: together standing, apart in a step.
    if (step == 1) {
        f.box(6, 13, 9, 14, look.bottom);
        f.box(6, 15, 9, 15, look.shoes);
        return;
    }
    const int front = step == 0 ? 4 : 5;
    const int back = step == 0 ? 9 : 10;
    for (const int x : {front, back}) {
        f.box(x, 13, x + 1, 14, look.bottom);
        f.box(x, 15, x + 1, 15, look.shoes);
    }
}

void figure(const Frame& f, const CharacterLook& look, Facing facing, int step) {
    const bool side = facing == Facing::Left || facing == Facing::Right;
    const Rgb shade = darker(look.top);
    // Body and arms.
    if (side) {
        f.box(5, 8, 10, 12, look.top);
        f.box(5, 12, 10, 12, shade);
        f.box(7, 8, 8, 11, shade); // the near arm
        f.box(7, 12, 8, 12, look.skin);
    } else {
        f.box(4, 8, 11, 12, look.top);
        f.box(4, 12, 11, 12, shade);
        f.box(3, 8, 3, 11, look.top);
        f.box(12, 8, 12, 11, look.top);
        f.put(3, 12, look.skin);
        f.put(12, 12, look.skin);
    }
    legs(f, look, facing, step);
    // The head, and the face where it is seen.
    f.box(5, 3, 10, 7, look.skin);
    if (facing == Facing::Down) {
        f.put(6, 5, eye);
        f.put(9, 5, eye);
    } else if (side) {
        f.put(6, 5, eye); // looking left (mirrored for right)
    }
    // Hair.
    const Rgb& h = look.hair_color;
    switch (look.hair) {
    case Hair::None:
        break;
    case Hair::Spiky:
        f.put(5, 0, h);
        f.put(7, 0, h);
        f.put(9, 0, h);
        [[fallthrough]];
    case Hair::Short:
    case Hair::Long:
        f.box(5, 1, 10, 2, h);
        if (facing == Facing::Up) {
            f.box(5, 3, 10, 6, h);
        } else if (side) {
            f.box(9, 3, 10, 5, h);
        } else {
            f.put(5, 3, h);
            f.put(10, 3, h);
        }
        if (look.hair == Hair::Long) {
            if (facing == Facing::Up) {
                f.box(4, 3, 11, 9, h);
            } else if (side) {
                f.box(10, 3, 11, 9, h);
            } else {
                f.box(4, 3, 4, 9, h);
                f.box(11, 3, 11, 9, h);
            }
        }
        break;
    }
    // Headwear, over the hair.
    const Rgb& w = look.headwear_color;
    switch (look.headwear) {
    case Headwear::None:
        break;
    case Headwear::Hood:
        f.box(4, 1, 11, 2, w);
        if (facing == Facing::Up) {
            f.box(4, 3, 11, 8, w);
        } else if (side) {
            f.box(9, 3, 11, 8, w);
        } else {
            f.box(4, 3, 4, 8, w);
            f.box(11, 3, 11, 8, w);
        }
        break;
    case Headwear::Hat:
        f.box(5, 0, 10, 2, w);
        f.box(3, 3, 12, 3, darker(w));
        break;
    }
}

} // namespace

engine::assets::Image character_sheet(const CharacterLook& look) {
    engine::assets::Image image;
    image.width = character_frame * 3;
    image.height = character_frame * 4;
    image.rgba.assign(std::size_t{image.width} * image.height * 4, 0);
    constexpr std::array<Facing, 4> rows{Facing::Down, Facing::Left, Facing::Right, Facing::Up};
    for (std::uint32_t row = 0; row < rows.size(); ++row) {
        for (std::uint32_t step = 0; step < 3; ++step) {
            const Frame f{image, step * character_frame, row * character_frame,
                          rows.at(row) == Facing::Right};
            figure(f, look, rows.at(row), static_cast<int>(step));
        }
    }
    return image;
}

std::optional<CharacterLook::Rgb> parse_rgb(std::string_view text) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return std::nullopt;
    }
    CharacterLook::Rgb rgb{};
    for (std::size_t i = 0; i < 3; ++i) {
        unsigned value = 0;
        const char* first = text.data() + (i * 2);
        const auto [end, failed] = std::from_chars(first, first + 2, value, 16);
        if (failed != std::errc{} || end != first + 2) {
            return std::nullopt;
        }
        rgb.at(i) = static_cast<std::uint8_t>(value);
    }
    return rgb;
}

} // namespace editor
