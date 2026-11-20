#pragma once

// The character generator (plan/02 B13, as RPG Maker's): a walking character drawn from a
// few choices and colours, as a sheet the Walk Animation plays: four rows (down, left,
// right, up) of three 16 x 16 frames (a step, standing, the other step).
//
// See docs/dev/editor.md (Characters).

#include "engine/assets/assets.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace editor {

/// What a generated character looks like.
struct CharacterLook {
    using Rgb = std::array<std::uint8_t, 3>;
    enum class Hair : std::uint8_t { Short, Long, Spiky, None };
    enum class Headwear : std::uint8_t { None, Hood, Hat };
    Hair hair = Hair::Short;
    Headwear headwear = Headwear::None;
    Rgb skin{238, 196, 160};
    Rgb hair_color{84, 52, 36};
    Rgb top{52, 96, 180};   ///< Tunic, shirt or robe.
    Rgb bottom{70, 60, 90}; ///< Trousers.
    Rgb shoes{60, 44, 36};
    Rgb headwear_color{140, 40, 40};
};

/// The size of one frame of a generated sheet, in pixels.
inline constexpr std::uint32_t character_frame = 16;

/// The sheet of `look`: 48 x 64 pixels, rows down, left, right, up; in each, the left foot
/// forward, standing, the right foot forward. Clear around the figure; the feet on the
/// bottom row of each frame.
[[nodiscard]] engine::assets::Image character_sheet(const CharacterLook& look);

/// "#rrggbb" as a colour; nothing if it is not one.
[[nodiscard]] std::optional<CharacterLook::Rgb> parse_rgb(std::string_view text);

} // namespace editor
