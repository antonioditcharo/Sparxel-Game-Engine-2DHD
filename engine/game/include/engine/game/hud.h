#pragma once

// On-screen displays without code (plan/02 K2): words and bars fixed to a place on the
// screen, not the map, such as the gold carried, the hero's HP, a score or a countdown.
// Their words name values in braces, filled each frame: {gold}, {hp:hero}, {var:score}.
//
// See docs/dev/ui.md (Screen Text, Screen Bar and Screen Picture).

#include "engine/foundation/reflect.h"
#include "engine/render/render.h"
#include "engine/world/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// Where on the screen something is fixed.
enum class ScreenAnchor : std::uint8_t {
    TopLeft,
    Top,
    TopRight,
    Left,
    Middle,
    Right,
    BottomLeft,
    Bottom,
    BottomRight,
};

/// "Screen Text": words fixed to the screen, with values in braces.
struct ScreenText {
    std::string text = "Gold: {gold}";
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0; ///< Pixels in from the anchor's sides.
    double y = 4.0;
    std::int64_t size = 1; ///< 1 or 2: twice as large.
    bool visible = true;
};

/// "Screen Bar": a bar filled as far as a value is towards its most, as HP is.
struct ScreenBar {
    std::string value = "{hp:hero}";    ///< What fills it...
    std::string most = "{max_hp:hero}"; ///< ...out of this.
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0;
    double y = 14.0;
    double width = 48.0; ///< Pixels.
    double height = 4.0;
    std::string color = "#dc4646";
    bool visible = true;
};

/// "Screen Picture": a picture fixed to the screen: an icon, a portrait, or a frame behind
/// words, stretched to any size by its 9-slice border so its corners stay crisp.
struct ScreenPicture {
    std::string image; ///< The GUID of a PNG asset.
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 0.0; ///< Pixels in from the anchor's sides.
    double y = 0.0;
    double width = 0.0;  ///< Pixels; 0 is the picture's own width.
    double height = 0.0; ///< Pixels; 0 is the picture's own height.
    /// 9-slice: the pixels at each edge kept as drawn while the middle stretches (a frame's
    /// corners and sides). 0 stretches the whole picture.
    double border = 0.0;
    double opacity = 1.0; ///< 0 (unseen) to 1.
    bool visible = true;
};

/// "Screen Button": words in a box on the screen that the player clicks with the mouse
/// (the "click" action): its Rules hear "clicked" (a menu of the game's own, a shop's Buy,
/// an adventure's verbs). Lit while the pointer is over it; dim, and not clicked, while
/// turned off. A click on it does not also walk the player (Point and Click).
struct ScreenButton {
    std::string text = "Button"; ///< With values in braces, as Screen Text's.
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0; ///< Pixels in from the anchor's sides.
    double y = 4.0;
    double width = 0.0;  ///< Pixels; 0 fits the words.
    double height = 0.0; ///< Pixels; 0 fits a line.
    bool enabled = true;
    bool visible = true;
};

/// "Screen Slider": a value the player sets by dragging a knob along a line with the mouse
/// (a volume, a difficulty, how much to buy): from `least` to `most` in `step`s. Its Rules
/// hear "changed" with the new value; logic reads and sets `value` as any property.
struct ScreenSlider {
    double value = 5.0;
    double least = 0.0;
    double most = 10.0;
    double step = 1.0; ///< 0 for any value between.
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0;
    double y = 4.0;
    double width = 64.0; ///< Pixels along the line.
    bool enabled = true;
    bool visible = true;
};

/// How far along its line a slider's value is, 0 to 1 (0 if `least` and `most` meet).
[[nodiscard]] double slider_fraction(const ScreenSlider& slider) noexcept;
/// The value at `fraction` (0 to 1, clamped) of the line: between least and most, on a step.
[[nodiscard]] double slider_value_at(const ScreenSlider& slider, double fraction) noexcept;

/// "Screen List": choices one under another that the player picks with the mouse (an
/// item to use, a save to load, a place to travel to). `items` is the choices separated by
/// commas, with values in braces as Screen Text's; the one picked is lit and is
/// `selected` (from 0; -1 for none), and its Rules hear "picked" with the choice's words.
struct ScreenList {
    std::string items = "First, Second, Third";
    std::int64_t selected = -1;
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0;
    double y = 4.0;
    double width = 0.0; ///< Pixels; 0 fits the widest choice.
    bool enabled = true;
    bool visible = true;
};

/// A list's choices: its items split at commas, each trimmed, empty ones left out.
[[nodiscard]] std::vector<std::string> list_items(std::string_view items);
/// A list's size for `count` choices, the widest `words_width` pixels, each line
/// `line_height` tall: 3 pixels round them, a line each.
[[nodiscard]] std::array<float, 2> list_size(const ScreenList& list, float words_width,
                                             float line_height, std::size_t count) noexcept;
/// Which choice a point `down` pixels below the list's top is on: -1 above or below them.
[[nodiscard]] std::int64_t list_row_at(float down, float line_height, std::size_t count) noexcept;

/// A button's size: its own, or the words (`words_width` pixels wide, `line_height` tall)
/// with 4 pixels round them.
[[nodiscard]] std::array<float, 2> button_size(const ScreenButton& button, float words_width,
                                               float line_height) noexcept;

/// How a Screen Layout places its children's on-screen elements.
enum class LayoutDirection : std::uint8_t {
    Down,   ///< One under another: a list, a party's HP bars.
    Across, ///< Side by side: hearts, a row of icons.
    Grid,   ///< In rows of `columns`, each cell as large as the largest: an item grid.
};

/// "Screen Layout": places the Screen Text, Bars and Pictures of its child objects together,
/// in order, so they line up by themselves (feature K1): a party's names and HP bars one
/// under another, hearts in a row. The block is fixed to the screen by its own Place, X
/// and Y; the children's own Places are not used.
struct ScreenLayout {
    ScreenAnchor anchor = ScreenAnchor::TopLeft;
    double x = 4.0;
    double y = 4.0;
    LayoutDirection direction = LayoutDirection::Down;
    double spacing = 2.0;     ///< Pixels between them.
    std::int64_t columns = 3; ///< For a grid: how many in a row.
    bool visible = true;      ///< Hidden: its children are hidden too.
};

/// Where a `w` x `h` thing's top-left goes on a `screen_w` x `screen_h` screen, fixed to
/// `anchor` and `x`, `y` pixels in from it (as Screen Text, Bars and Pictures are).
[[nodiscard]] std::array<float, 2> screen_place(ScreenAnchor anchor, double x, double y, float w,
                                                float h, float screen_w, float screen_h);

/// The top-left of each thing of `sizes` (width, height), placed by `layout` on a
/// `screen_w` x `screen_h` screen. In a column, things line up on the side of its Place
/// (right for a Place on the right, the middle for one in the middle), as do rows.
[[nodiscard]] std::vector<std::array<float, 2>>
lay_out(const ScreenLayout& layout, const std::vector<std::array<float, 2>>& sizes, float screen_w,
        float screen_h);

/// One piece of a 9-slice: the source pixels and where they go.
struct SlicePiece {
    render::Rect source;
    render::Rect destination;
};

/// The pieces drawing a `width` x `height` picture into `destination` with `border` pixels
/// at each edge unstretched: nine of them (fewer where a piece has no size), or one when
/// `border` is 0. The border is narrowed to fit a picture or a destination too small for it.
[[nodiscard]] std::vector<SlicePiece> nine_slice(float width, float height,
                                                 render::Rect destination, float border);

/// `text` with each {name} replaced by `value(name)`; a doubled brace ({{) stays one.
[[nodiscard]] std::string fill_values(std::string_view text,
                                      const std::function<std::string(std::string_view)>& value);

/// Registers the components and the "screen_anchor" choice list.
void register_hud(reflect::Registry& registry);
/// Makes them usable as components of `world`.
bool register_hud_components(world::World& world);

} // namespace engine::game
