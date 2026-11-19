#pragma once

// Lighting maps (plan/02 B9, as Sprite Illuminator makes them): a normal map made from a
// picture, so the Diorama style's sun and lights catch its rounded edges and bumps, and a
// glow map of its brightest colours, which shine in the dark. Saved beside the picture as
// "<name>_normal.png" and "<name>_glow.png", they are used wherever it is drawn.
//
// See docs/dev/assets.md, *Lighting maps*.

#include "engine/assets/assets.h"

#include <cstdint>

namespace engine::assets {

/// Where a normal map's height comes from.
enum class Relief : std::uint8_t {
    Rounded, ///< The picture's outline, rounded over `bevel` pixels: characters and props.
    Bumpy,   ///< Its brightness, bright parts raised: stone, bark, cobbles.
    Both,    ///< The two added.
};

struct NormalMapSettings {
    Relief relief = Relief::Rounded;
    float strength = 1.0F;   ///< How steep the slopes are: 0 flat, 1 as shaped, up to 8.
    std::uint32_t bevel = 3; ///< Pixels over which edges round off (1 to 16).
    /// Cells drawn apart, as a tileset's tiles or a sheet's frames: their edges are edges,
    /// and no slope crosses them. 0: the whole picture is one.
    std::uint32_t cell_width = 0;
    std::uint32_t cell_height = 0;
};

/// A normal map for `picture`, the same size: red along the picture to the right, green up
/// it, blue out of it, each 0 to 255 for -1 to 1 (flat is 128, 128, 255). Clear pixels are
/// flat; every pixel is opaque.
[[nodiscard]] Image make_normal_map(const Image& picture, const NormalMapSettings& settings);

/// A glow map for `picture`, the same size: its pixels at least `threshold` bright (0 to 1,
/// the brightest of red, green and blue) as they are, opaque; the rest clear.
[[nodiscard]] Image make_glow_map(const Image& picture, float threshold);

} // namespace engine::assets
