#pragma once

// Classic 2D scenes (plan/04 §3): entities with a 2D transform and a sprite or a tilemap,
// drawn through a pixel-snapped orthographic camera, and sprites animated from sprite
// sheets. This is the first place where the world, the asset database and the renderer
// work together.
//
// See docs/dev/scene2d.md.

#include "engine/assets/assets.h"
#include "engine/assets/gltf.h"
#include "engine/foundation/guid.h"
#include "engine/foundation/reflect.h"
#include "engine/render/render.h"
#include "engine/world/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine::scene2d {

/// Where an entity is, in world units: one unit is one tile of `ppu` pixels (plan/04 §2).
struct Transform2D {
    double x = 0.0;
    double y = 0.0; ///< Grows downwards, as on screen.
    /// Draw order between groups: higher layers are drawn on top. Within a layer, lower
    /// on screen (larger y) is drawn on top.
    std::int32_t layer = 0;
};

/// The image an entity shows.
struct Sprite {
    std::string image; ///< The GUID of a PNG asset, as 32 hexadecimal digits.
    /// Colours swapped as it is drawn, "#46b450>#c83c3c, #aaf0a0>#f0b0b0": one picture as
    /// a red slime and a green one (a palette swap). Empty draws it as it is.
    std::string recolor;
    /// The point of the image placed at the transform's position, from 0 (left, top) to 1
    /// (right, bottom). The default, bottom centre, puts a character's feet on its
    /// position, which is what y-sorting needs.
    double pivot_x = 0.5;
    double pivot_y = 1.0;
    std::uint8_t tint_r = 255;
    std::uint8_t tint_g = 255;
    std::uint8_t tint_b = 255;
    std::uint8_t tint_a = 255;
    bool visible = true;
    /// For a sprite sheet: the size of one frame in pixels. 0 uses the whole image. Frames
    /// are numbered from 0, left to right, then top to bottom; numbers past the last frame
    /// wrap around.
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::uint32_t frame = 0;
    /// Turns the picture about its pivot, in degrees, clockwise as seen (in the Diorama
    /// style, in the billboard's own plane).
    double rotation = 0.0;
    /// Stretches the picture about its pivot: 2 is twice as wide (or tall); a negative
    /// number flips it.
    double scale_x = 1.0;
    double scale_y = 1.0;
    /// Effects on its pixels (render::SpriteEffects), in both styles: a flash of
    /// `flash_color`, from 0 (none) to 1 (all of it), as a hit makes; an outline round its
    /// opaque pixels ("#000000", or with an alpha "#00000080"; empty for none); and
    /// dissolving away, from 0 (whole) to 1 (gone).
    double flash = 0.0;
    std::string flash_color = "#ffffff";
    std::string outline;
    double dissolve = 0.0;
};

/// The render::SpriteEffects `sprite` asks for. Colours that are not "#rrggbb" or
/// "#rrggbbaa" are none (a flash then white); amounts are clamped to 0 to 1, NaN as 0.
[[nodiscard]] render::SpriteEffects sprite_effects(const Sprite& sprite);

/// Where the corners of `sprite`'s picture go relative to its position, for a frame
/// `width` x `height` (in any unit; the result is in the same one), x to the right and y
/// down: the frame's top-left, top-right, bottom-right and bottom-left, after it is scaled
/// and then turned about its pivot. A rotation or scale that is not a finite number counts
/// as none (0 and 1).
[[nodiscard]] std::array<std::array<double, 2>, 4>
sprite_corners(const Sprite& sprite, double width, double height) noexcept;

/// "Animation": plays a run of a sprite sheet's frames on the entity's Sprite.
struct SpriteAnimation {
    /// A tag of the sprite's sheet to play, such as "walk" (its frames, their own times,
    /// and its direction, from the sheet's ".sheet" file: SheetInfo). Empty, or a tag the
    /// sheet does not have, plays `first_frame` on at `frames_per_second` instead.
    std::string tag;
    std::uint32_t first_frame = 0;
    std::uint32_t frame_count = 1;
    double frames_per_second = 8.0;
    bool loop = true;
    bool playing = true;
    double elapsed = 0.0; ///< Seconds since it started playing (hidden; saved with scenes).

    /// Starts again from the first frame.
    void play() {
        elapsed = 0.0;
        playing = true;
    }
    /// Stops on the current frame.
    void stop() { playing = false; }
    /// Plays a tag of the sheet from its start (an empty name: the frames by number).
    void play_tag(const std::string& name) {
        tag = name;
        play();
    }
};

/// Which way a tag's frames play.
enum class TagDirection : std::uint8_t { Forward, Reverse, PingPong };

/// A named run of a sheet's frames: an animation such as "walk" or "idle".
struct SheetTag {
    std::string name;
    std::uint32_t from = 0; ///< The first frame, counting from 0.
    std::uint32_t to = 0;   ///< The last frame, included.
    TagDirection direction = TagDirection::Forward;
};

/// A rectangle of a frame, in its pixels from its top-left: a hit or hurt box. Empty (0
/// wide or high) for none.
struct FrameBox {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool empty() const noexcept { return width == 0 || height == 0; }
    friend bool operator==(const FrameBox&, const FrameBox&) = default;
};

/// A frame's hit and hurt boxes (plan/02 B7): where its attack lands, and where it can be
/// struck.
struct FrameBoxes {
    FrameBox hit;
    FrameBox hurt;
    friend bool operator==(const FrameBoxes&, const FrameBoxes&) = default;
};

/// What a sprite sheet holds, as Sprite Studio saves it beside the PNG ("art/hero.png" has
/// "art/hero.sheet"): the size of a frame, how long each frame shows, and its tags.
struct SheetInfo {
    std::uint32_t frame_width = 0;
    std::uint32_t frame_height = 0;
    std::vector<std::uint32_t> durations_ms; ///< One per frame, 1 to 65,535.
    std::vector<SheetTag> tags;
    /// Frame events (plan/02 B6): a name for each frame that cues something when an
    /// animation reaches it ("footstep", "hit"), empty for none. Fewer than the frames
    /// means the rest have none.
    std::vector<std::string> events;
    /// Each frame's hit and hurt boxes; fewer than the frames means the rest have none.
    std::vector<FrameBoxes> boxes;

    [[nodiscard]] const SheetTag* find(std::string_view name) const noexcept;
    /// The frames a tag shows, in the order it shows them, each with how long it shows
    /// (milliseconds): forward, backward, or forward and back without repeating the ends.
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>>
    sequence(const SheetTag& tag) const;
};

/// The ".sheet" format name and version.
inline constexpr std::string_view sheet_format = "sparxel.sheet";
inline constexpr int sheet_version = 1;

/// The ".sheet" file of a picture: "art/hero.png" gives "art/hero.sheet".
[[nodiscard]] std::string sheet_path(std::string_view image_path);
/// Reads ".sheet" text. Nothing, with `error` in plain language, if it is not one. Tags
/// outside the frames are dropped.
[[nodiscard]] std::optional<SheetInfo> parse_sheet(std::string_view text, std::string& error);
/// The ".sheet" text of a sheet, with sorted keys, so the same sheet is always the same
/// text.
[[nodiscard]] std::string sheet_to_text(const SheetInfo& sheet);

/// The sheets of a project's pictures, read on first use, as TextureCache reads textures.
class SheetLibrary {
public:
    explicit SheetLibrary(const assets::AssetDatabase& assets);
    /// The sheet of an image asset (its GUID as text, as Sprite::image holds it), or null
    /// if it has none or it cannot be read.
    const SheetInfo* find(std::string_view image);
    /// Forgets what `changes` touched, so the next find() reads it again (hot reload).
    void apply(const assets::AssetChanges& changes);

private:
    const assets::AssetDatabase& assets_;
    std::unordered_map<std::string, std::optional<SheetInfo>> sheets_;
};

/// A tilemap's tiles, parsed. Cells hold tile numbers, or -1 for none.
struct TileGrid {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::int32_t> cells; ///< Row by row.

    [[nodiscard]] std::int32_t at(std::uint32_t x, std::uint32_t y) const noexcept {
        return x < width && y < height ? cells[(std::size_t{y} * width) + x] : -1;
    }

    friend bool operator==(const TileGrid&, const TileGrid&) = default;
};

/// The largest tilemap side, in tiles.
inline constexpr std::uint32_t max_tilemap_side = 4096;

/// Parses tilemap text: one line per row, cells separated by commas, each a tile number
/// or empty (or -1) for no tile. Short rows are filled with empty cells. Returns nothing,
/// with `error` in plain language, for a cell that is not a whole number from -1 up, or a
/// map wider or taller than max_tilemap_side.
[[nodiscard]] std::optional<TileGrid> parse_tiles(std::string_view text, std::string& error);
/// The text form of `grid`, which parse_tiles() reads back unchanged.
[[nodiscard]] std::string format_tiles(const TileGrid& grid);

/// "Tilemap": a grid of tiles from one tileset image, with its top-left corner at the
/// entity's position. Tiles are numbered like sprite sheet frames. They are stored as text
/// (see parse_tiles()) so scene files stay readable and diff well.
struct Tilemap {
    /// In the Diorama style, how tall each tile stands, in tiles: 0 is flat ground; above,
    /// every tile is a block with the tile's picture on its top and sides.
    double height = 0.0;
    /// How solid the tiles look, 0 (invisible) to 1: below 1 they are see-through, for
    /// water over a riverbed or glass blocks; what is behind shows through, blended.
    double opacity = 1.0;
    double animation_fps = 4.0;
    double elapsed = 0.0; ///< Seconds the animation has run (hidden; saved with scenes).
    std::string tileset;  ///< The GUID of a PNG asset.
    /// The sound walking on this map's tiles makes (grass, stone, wood): a sound in assets/;
    /// empty to leave the walker's own.
    std::string footsteps;
    std::string tiles;
    /// Each tile's own height, for terrain built tile by tile: text like `tiles`, a number
    /// per cell (0 to 16, halves and quarters allowed); an empty cell, or a cell past the
    /// text, stands at `height`.
    std::string heights;
    std::string shapes;          ///< See `solid`: "12 top, 13 bottom, 7 none".
    std::uint32_t tile_size = 0; ///< In pixels, square. 0 uses the camera's PPU.
    /// Animated tiles, for water, lava or flowers in the wind: each tile shows itself and
    /// then the next animation_frames - 1 tiles of the tileset in turn, at animation_fps
    /// frames a second (1 frame: still). Put animated tiles on their own map.
    std::uint32_t animation_frames = 1;
    bool visible = true;
    /// Every tile of this map blocks movement (see sweep()). Paint walls on their own map,
    /// hidden if the art is on another one.
    bool solid = false;
    /// On a solid map, tiles that block only part of their square, by their number in the
    /// tileset: "12 top, 13 bottom, 7 none" (full, none, top, bottom, left or right: a
    /// half). Tiles not named block all of it. See tile_shape().
    /// Painting picks each tile's shape from its neighbours: for a tileset expanded from
    /// auto-tile blocks (assets/autotile.h), where each 48 tiles are one terrain.
    bool autotile = false;
    /// Baked light (bake_lighting()): at each corner of the tiles, (columns + 1) x
    /// (rows + 1) of them, what baked lights add there and how open the corner is, as
    /// "rrggbboo" in hexadecimal (light 0 to 4 in 0 to ff; openness 0 to 1), corners side
    /// by side, a line a row. Written by the bake; empty for none.
    std::string baked_light;
    /// Smooth terrain (plan/02 C14): in the Diorama style, `heights` are at the tiles'
    /// corners, (columns + 1) x (rows + 1) of them, and each tile's top slopes between its
    /// four, as hills and valleys; the map's edges drop to the ground. Off, each tile is a
    /// block of its own height.
    bool smooth = false;
    /// The alpha the tiles are drawn with, 0 to 255, from `opacity`.
    [[nodiscard]] std::uint8_t alpha() const noexcept;
    /// How far each tile is through its animation: 0 to animation_frames - 1.
    [[nodiscard]] std::uint32_t frame() const noexcept;
    /// The tile shown for painted tile `number` now: `number` plus frame(), or `number`
    /// itself if that would run past the tileset's `tile_count`.
    [[nodiscard]] std::uint32_t shown(std::uint32_t number,
                                      std::uint64_t tile_count) const noexcept;

    /// The parsed tiles, kept until `tiles` changes. Null, with `error` set, if the text
    /// is not valid.
    [[nodiscard]] const TileGrid* grid(std::string& error) const;
    /// How tall the tile in column `x`, row `y` stands: its own height, else `height`;
    /// kept within 0 to 16. For smooth terrain, the height of the corner at the top-left
    /// of that tile (column `x` may be the map's width, row `y` its height).
    [[nodiscard]] double height_at(std::uint32_t x, std::uint32_t y) const;
    /// The top of the map at (`column`, `row`), in tiles from its top-left corner,
    /// fractions allowed: a block's height, or on smooth terrain the slope between the
    /// tile's corners, as it is drawn (two triangles split from north-west to south-east).
    [[nodiscard]] double surface_at(double column, double row) const;
    /// The baked light at corner (x, y) of the tiles: red, green and blue added, and how
    /// open it is (1 in the open; less in a corner between walls). {0, 0, 0, 1} where
    /// nothing is baked.
    [[nodiscard]] std::array<float, 4> baked_at(std::uint32_t x, std::uint32_t y) const;

private:
    // A cache only: not reflected, so never saved.
    mutable std::vector<std::vector<double>> heights_; ///< By row; NaN for the map's height.
    mutable std::string parsed_text_;
    mutable std::string parse_error_;
    mutable std::string heights_text_;
    mutable std::optional<TileGrid> parsed_;
    mutable bool parsed_once_ = false;
    mutable bool heights_once_ = false;
    mutable std::vector<std::vector<std::array<float, 4>>> baked_;
    mutable std::string baked_text_;
    mutable bool baked_once_ = false;
};

/// Reads a heights grid: rows by line, cells by comma, each a number from 0 to 16 or empty.
/// Cells that are not numbers count as empty.
[[nodiscard]] std::vector<std::vector<double>> parse_heights(std::string_view text);
/// The text of a heights grid, as parse_heights() reads it: empty cells stay empty, and
/// whole numbers have no decimals.
[[nodiscard]] std::string format_heights(const std::vector<std::vector<double>>& heights);

/// "Model": a 3D model (a glTF asset, ".gltf" or ".glb") standing at the position in the
/// Diorama style, as HD-2D games set their characters among 3D scenery: houses, trees,
/// bridges. Its ground is the position; its model units are tiles times `scale`. Not drawn
/// in Classic 2D.
struct Model3D {
    std::string model;   ///< The GUID of a glTF asset.
    double scale = 1.0;  ///< Tiles to a model unit.
    double turn = 0.0;   ///< Degrees about the up axis, clockwise seen from above.
    double height = 0.0; ///< Tiles above the ground.
    bool visible = true;
    bool casts_shadow = true; ///< In the sun's shadows.
};

/// "Parallax": a Sprite that scrolls slower (or faster) than the map as the camera moves, and
/// repeats to fill the view: far hills, clouds, a sky, in Classic 2D (plan/02 D11).
struct Parallax {
    /// How much it moves with the camera across: 0 stays fixed on the screen (the far
    /// sky), 1 moves with the map, 0.5 at half the speed (distant hills); above 1, nearer
    /// than the map.
    double scroll_x = 0.5;
    double scroll_y = 0.5; ///< The same, up and down.
    bool repeat_x = true;  ///< Repeats side by side to fill the view's width.
    bool repeat_y = false; ///< Repeats one above another to fill its height.
};

/// "Light": a point light, such as a torch, a lamp or a glowing window. It lights the
/// ground, walls and characters around it, fading smoothly to nothing at its radius. In the
/// Diorama style it adds to the scene's ambient and sun light; in Classic 2D it brightens
/// what is drawn (draw_lights_2d()). Either way it shows best at dusk and night.
struct Light {
    std::uint8_t color_r = 255; ///< A warm torchlight by default.
    std::uint8_t color_g = 180;
    std::uint8_t color_b = 110;
    double intensity = 1.0; ///< 0 to 8.
    double radius = 4.0;    ///< In tiles, 0 to 32.
    double height = 1.0;    ///< Above the ground, in tiles, 0 to 16.
    bool enabled = true;
    /// Baked (plan/02 D17): worked out once by bake_lighting(), with the shadows of blocks
    /// and hills, into the maps around it, instead of every frame. A scene may have as many
    /// as it likes (only the 16 nearest live lights count); they light the ground, and
    /// sprites and models standing in them, but do not move.
    bool baked = false;
};

/// Registers Transform2D ("transform_2d"), Sprite ("sprite"), SpriteAnimation
/// ("sprite_animation"), Tilemap ("tilemap"), Light ("light"), Parallax ("parallax") and
/// Model3D ("model_3d") with their editor metadata.
void register_types(reflect::Registry& registry);
/// Makes them all usable as components of `world`. register_types() must have run first.
bool register_components(world::World& world);

/// Advances every playing SpriteAnimation by `seconds` and sets its Sprite's frame, and
/// moves animated tilemaps on (they loop, and never finish). Call it once per fixed step.
/// Returns the entities whose animation reached its end this time (animations that do not
/// loop), so their "finished" event can be sent. With `frame_events`, each animation that
/// reached a frame with a frame event (its sheet's `events`) adds itself and the event's
/// name, so its "frame_event" can be sent.
std::vector<world::Entity>
animate(world::World& world, double seconds, SheetLibrary* sheets = nullptr,
        std::vector<std::pair<world::Entity, std::string>>* frame_events = nullptr);

/// One object's hit box overlapping another's hurt box: `attacker` strikes `target`.
struct HitContact {
    world::Entity attacker;
    world::Entity target;
};

struct Box;
/// The contacts in `world` now: every object with a Transform2D, a Sprite and (from its
/// sheet) a hit box on the frame it shows, against every other's hurt box on its frame,
/// boxes placed as the picture is (its pivot and scale, flipped with it; turning is not
/// followed). `ppu` sizes pixels.
[[nodiscard]] std::vector<HitContact> hit_contacts(const world::World& world, SheetLibrary& sheets,
                                                   std::uint32_t ppu);

/// An axis-aligned box in world units, such as a character's feet. Edges are half-open:
/// a box whose right edge is exactly on a tile's left edge does not overlap it.
struct Box {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

enum class Axis : std::uint8_t { X, Y };

/// How much of its square a solid tile blocks.
enum class TileShape : std::uint8_t {
    Full,
    None,
    Top,
    Bottom,
    Left,
    Right,
    /// A slope rising to the right (low on the left, the tile's top on the right) or to
    /// the left. It blocks nothing as a box; side-view characters stand on its surface
    /// (slope_floor()).
    SlopeRight,
    SlopeLeft,
};

/// The shapes a Tilemap's `shapes` text gives, by tile number: "12 top, 13 bottom" (pairs
/// separated by commas, each a tile number then a shape; any case). Pairs that cannot be
/// read are skipped, and `error` says which (empty if none).
[[nodiscard]] std::vector<std::pair<std::int32_t, TileShape>>
parse_tile_shapes(std::string_view text, std::string& error);
/// The part of a `tile`-sized square (0, 0 at its top-left) that `shape` blocks; nothing
/// for None.
[[nodiscard]] std::optional<Box> tile_shape_box(TileShape shape, double tile) noexcept;

/// The highest slope surface (TileShape::SlopeRight or SlopeLeft on a solid map) under the
/// point x, between `top` and `bottom` (map units, y down): where feet there would stand.
/// Nothing if no slope is there.
[[nodiscard]] std::optional<double> slope_floor(const world::World& world, double x, double top,
                                                double bottom, std::uint32_t ppu);

/// True if the boxes share more than an edge (or a rounding error of one).
[[nodiscard]] bool overlaps(const Box& a, const Box& b) noexcept;

/// Like sweep(), against boxes instead of tiles: how far `box` can move by `distance`
/// along `axis` before it would enter one of `obstacles`. Obstacles it already overlaps do
/// not block.
[[nodiscard]] double sweep_boxes(const Box& box, Axis axis, double distance,
                                 const std::vector<Box>& obstacles);

/// How far `box` can move by `distance` along `axis` before it would enter a tile of a
/// solid Tilemap in `world`, visible or not: `distance` itself if nothing is in the way,
/// else the distance to the first such tile's edge (0 when touching it). Tiles the box
/// already overlaps do not block, so a character placed inside a wall can walk out.
/// `ppu` sizes tilemaps whose tile_size is 0.
[[nodiscard]] double sweep(const world::World& world, const Box& box, Axis axis, double distance,
                           std::uint32_t ppu);

/// True if `box` overlaps a tile of a solid Tilemap in `world` (more than an edge): where
/// a character cannot stand. `ppu` sizes tilemaps whose tile_size is 0.
[[nodiscard]] bool solid_at(const world::World& world, const Box& box, std::uint32_t ppu);

/// True if nothing solid (tiles, as solid_at() finds them) lies on the straight line from
/// (x0, y0) to (x1, y1): whether one can see the other. Looks every eighth of a tile; never
/// further than 256 tiles.
[[nodiscard]] bool line_of_sight(const world::World& world, double x0, double y0, double x1,
                                 double y1, std::uint32_t ppu);

/// The area the tilemaps of `world` cover, in units: the smallest box around every
/// tilemap's grid (visible or not, solid or not). Nothing if no tilemap has tiles. `ppu`
/// sizes tilemaps whose tile_size is 0.
[[nodiscard]] std::optional<Box> map_bounds(const world::World& world, std::uint32_t ppu);

/// Where a camera looking at (x, y) must look so that its view stays inside `bounds`.
/// `view` is what it shows around the point, relative to it (left and top negative; an
/// infinite edge is never limited). On each axis the view moves in as little as it can; if
/// it is larger than `bounds`, it is centred on them. Returns {x, y}.
[[nodiscard]] std::array<double, 2> clamp_view(double x, double y, const Box& view,
                                               const Box& bounds) noexcept;

/// A pixel-perfect orthographic camera.
struct Camera2D {
    double x = 0.0; ///< The world position at the centre of the target, in units.
    double y = 0.0;
    std::uint32_t ppu = 16;  ///< Pixels per unit, the project setting (8 to 64).
    std::uint32_t scale = 1; ///< Whole-number upscale of every pixel (1 = none).
};

/// GPU textures for image assets, loaded on first use.
/// One colour of a picture drawn as another (a palette swap). Alpha is kept as it was.
struct ColorSwap {
    std::array<std::uint8_t, 3> from{};
    std::array<std::uint8_t, 3> to{};
};

/// The swaps of a Sprite's `recolor`: "#rrggbb>#rrggbb" pairs, split by commas or spaces.
/// Pairs that cannot be read are skipped.
[[nodiscard]] std::vector<ColorSwap> parse_recolor(std::string_view text);
/// The text of `swaps`, as parse_recolor() reads it.
[[nodiscard]] std::string recolor_text(const std::vector<ColorSwap>& swaps);
/// Swaps the colours of `image` (RGBA, 4 bytes a pixel); see-through pixels stay.
void recolor_image(assets::Image& image, const std::vector<ColorSwap>& swaps);
/// The different colours `image` uses, in the order first seen (see-through pixels left
/// out), at most `most` of them: what a palette swap can change.
[[nodiscard]] std::vector<std::array<std::uint8_t, 3>> image_colors(const assets::Image& image,
                                                                    std::size_t most = 64);

class TextureCache {
public:
    TextureCache(render::Device& device, const assets::AssetDatabase& assets);

    /// The texture for an image asset, or null if it is unknown, not an image, or cannot
    /// be decoded or uploaded (see last_error()). Pictures up to max_packed pixels each way
    /// are packed into shared atlas pages (render::Device::create_region()), so sprites and
    /// tiles drawn together share draw calls; nobody needs to make sprite sheets for that.
    /// Pictures with normal or glow maps keep textures of their own, so the maps line up.
    const render::Texture* get(Guid image);
    static constexpr std::uint32_t atlas_size = 2048; ///< Each atlas page, square.
    static constexpr std::uint32_t max_packed = 256;
    /// Whether get() packs pictures into atlas pages (it does unless turned off). Only
    /// pictures loaded after a change are affected.
    void set_packing(bool pack) noexcept { packing_ = pack; }
    /// Atlas pages made so far.
    [[nodiscard]] std::size_t atlas_pages() const noexcept { return pages_.size(); }
    /// The same, with its colours swapped by `recolor` (a Sprite's); each recolouring is
    /// made once and kept. An empty `recolor` is get(image).
    const render::Texture* get(Guid image, std::string_view recolor);

    /// Forgets textures whose assets were modified or removed, so the next get() loads
    /// them again. (A moved asset keeps its GUID and content, so its texture stays.) Call
    /// it with every result of AssetDatabase::refresh() to hot-reload images.
    void apply(const assets::AssetChanges& changes);

    /// A soft round light, white in the middle fading to black at the rim (128 x 128), for
    /// Classic 2D's lights. Made once; null if it cannot be.
    const render::Texture* light_falloff();

    /// The normal map beside an image asset: the PNG of the same name ending "_normal"
    /// ("art/hero_normal.png" for "art/hero.png"), the same size as it. Null if there is
    /// none. Found once and kept until assets change (apply()).
    const render::Texture* normal_map(Guid image);
    /// The same for its glow map, ending "_glow".
    const render::Texture* glow_map(Guid image);

    /// A model read from a glTF asset, with a texture for each of its meshes (its own, or
    /// plain white for its colour alone).
    struct LoadedModel {
        assets::Model model;
        std::vector<const render::Texture*> textures; ///< One for each mesh.
    };
    /// The model of a glTF asset (its buffers and pictures read beside it), read once and
    /// kept; null, with last_error(), if it cannot be read.
    const LoadedModel* model(Guid asset);

    [[nodiscard]] std::size_t size() const noexcept { return textures_.size(); }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    render::Device& device_;
    const assets::AssetDatabase& assets_;
    /// The image asset's pixels, or nothing (with last_error()).
    std::optional<assets::Image> load(Guid image);
    /// Whether the image has a normal or glow map beside it.
    [[nodiscard]] bool has_lighting_maps(const assets::AssetRecord& record) const;
    /// A region of an atlas page holding `image`, on a page with room; null if none can.
    std::unique_ptr<render::Texture> pack(const assets::Image& image);

    /// An atlas page: shelves of pictures, filled left to right, top to bottom.
    struct AtlasPage {
        std::unique_ptr<render::Texture> texture;
        std::uint32_t shelf_top = 0;    ///< The current shelf's top.
        std::uint32_t shelf_height = 0; ///< Its tallest picture so far, and the gap.
        std::uint32_t next_x = 0;
    };
    bool packing_ = true;
    /// Pixels of pages held by pictures since reloaded: past half a page, every packed
    /// picture is dropped and packed again as it is next drawn.
    std::uint64_t wasted_ = 0;
    std::vector<AtlasPage> pages_; ///< Before the textures: regions go first.
    std::unordered_map<Guid, std::unique_ptr<render::Texture>, GuidHash> textures_;
    /// Normal and glow maps, each a texture of its own.
    std::unordered_map<Guid, std::unique_ptr<render::Texture>, GuidHash> maps_;
    std::unique_ptr<render::Texture> falloff_;
    /// Recoloured textures, by "<guid> <recolor>".
    std::unordered_map<std::string, std::unique_ptr<render::Texture>> recolored_;
    /// Models, and the textures their meshes own; a model that cannot be read is kept as
    /// null so it is not read again every frame.
    std::unordered_map<Guid, std::unique_ptr<LoadedModel>, GuidHash> models_;
    std::vector<std::unique_ptr<render::Texture>> model_textures_;
    std::unique_ptr<render::Texture> white_;
    /// normal_map() and glow_map(), by the image and the ending; null kept for none.
    std::unordered_map<std::string, const render::Texture*> beside_;
    const render::Texture* beside(Guid image, std::string_view ending);
    std::string last_error_;
};

struct DrawStats {
    std::size_t drawn = 0;   ///< Sprites and tiles drawn.
    std::size_t missing = 0; ///< Sprites and tilemaps whose image or tiles could not be used.
};

/// How strongly Classic 2D's lights show under a look tinted `tint_r`, `tint_g`, `tint_b`
/// (the day and night's colour and brightness times the look's): a quarter in full
/// daylight, all of it in the dark, so torches show at night, and only a little at noon.
[[nodiscard]] double lights_2d_strength(double tint_r, double tint_g, double tint_b);

/// How Classic 2D's lights are shaded by the look drawn before them.
struct Lights2DShading {
    /// Scales every light, 0 to 1: see lights_2d_strength().
    double strength = 1.0;
    /// The tint the look multiplied the frame by (night's dark blue). Lights brighten the
    /// frame by one plus the light divided by it, so what shows is the scene times (that
    /// tint plus the light): a torch keeps its own warm colour in a blue night.
    std::array<double, 3> darkening{1.0, 1.0, 1.0};
    /// The look's saturation: 0 makes the lights grey too, as a black and white look is.
    double saturation = 1.0;
};

/// Classic 2D's lights: brightens what `target` holds around every enabled Light with a
/// Transform2D, in the light's colour (what is there times one plus the light, so the art's
/// own detail shows), fading to nothing at its radius. The glow's middle is `height` tiles
/// above the position, where a torch's flame is. An intensity above 1 brightens more, in
/// steps. The game draws them over the look's result (the frame, already darkened by
/// night), so nothing is lost to brightness the frame cannot hold. Returns how many were
/// drawn.
std::size_t draw_lights_2d(const world::World& world, TextureCache& textures,
                           render::SpriteRenderer& sprites, render::RenderTarget& target,
                           const Camera2D& camera, const Lights2DShading& shading = {});

/// Draws every visible entity that has a Transform2D and a Sprite or a Tilemap into
/// `target`, cleared to `clear`, as seen by `camera`. Positions are snapped to whole
/// pixels, so pixel art never shimmers. Only the tiles in view are drawn.
DrawStats draw_world(const world::World& world, TextureCache& textures,
                     render::SpriteRenderer& sprites, render::RenderTarget& target,
                     const Camera2D& camera, render::Color clear);

// ---------------------------------------------------------------------------------------
// The Diorama style (plan/04 §4): the same entities in 3D
// ---------------------------------------------------------------------------------------

/// The camera of the Diorama style. A map position (x, y) is the world point (x, 0, y): the
/// ground is the x-z plane, up is +y. The camera orbits the point it looks at.
struct DioramaCamera {
    double x = 0.0; ///< The map point looked at, in tiles.
    double y = 0.0;
    /// How far the camera looks down, in degrees: 90 is straight down. Kept from 5 to 85.
    double pitch = 35.0;
    /// Turning around the point, in degrees: 0 looks towards smaller map y ("north"), as
    /// the 2D view does.
    double yaw = 0.0;
    double distance = 12.0; ///< From the point, in tiles.
    double fov = 30.0;      ///< Vertical field of view, in degrees: narrow is the HD-2D look.
    std::uint32_t ppu = 16; ///< Pixels per tile, to size sprites and tiles.
};

/// Where the sun is by default: in the south-west (behind the default camera, a little to
/// its left), 55 degrees above the horizon. Shadows fall up the screen and to the right.
constexpr double default_sun_angle = 215.0;
constexpr double default_sun_height = 55.0;

/// The direction towards the sun, a unit vector, from where it is: `angle` degrees
/// clockwise from north (up the map: 90 is east, 180 south) and `height` degrees above the
/// horizon (90 is straight overhead).
[[nodiscard]] render::Vec3 sun_direction(double angle, double height) noexcept;

/// The shape of the shadow each sprite casts in the Diorama style.
enum class ShadowShape : std::uint8_t {
    Cast,  ///< Its own silhouette, laid on the ground away from the sun.
    Round, ///< A soft round blob under its feet, whatever the sun.
};

/// The light of a diorama: every face is lit by `ambient` plus `diffuse` times how much it
/// faces the sun.
struct DioramaLight {
    /// Towards the sun; need not be normalised.
    render::Vec3 sun = sun_direction(default_sun_angle, default_sun_height);
    float ambient = 0.55F;
    float diffuse = 0.45F;
    /// How dark each sprite's shadow is, 0 (none) to 1. Shadows lie on the top of the
    /// highest tile under the sprite's feet; with no tile there, none is drawn.
    float sprite_shadow = 0.35F;
    ShadowShape shadows = ShadowShape::Cast;
    /// How dark the sun's shadows of blocks are, 0 (none) to 1: walls and houses shadow
    /// the ground, other blocks and characters on the side away from the sun.
    float block_shadow = 0.45F;
};

/// The sun's shadow map for a diorama (render::SunShadow): its view covers the maps, as
/// high as their tallest block, looking along the sun's rays. Nothing with no maps, no
/// block shadow, or the sun set. Maps wider than max_sun_shadow_tiles are covered around
/// the camera instead, so the map's texels stay small.
[[nodiscard]] std::optional<render::SunShadow> diorama_sun_shadow(const world::World& world,
                                                                  const DioramaCamera& camera,
                                                                  const DioramaLight& light);
constexpr double max_sun_shadow_tiles = 96.0;

/// How far the shadow of a point one tile up reaches across the ground, as (x, y) on the
/// map, with the sun in direction `sun`: away from the sun, longer as it gets lower, and
/// at most max_shadow_length. Nothing with the sun at or below the horizon.
[[nodiscard]] std::optional<std::array<double, 2>> shadow_offset(render::Vec3 sun) noexcept;
constexpr double max_shadow_length = 3.0;

/// The top of the highest visible tile under map point (x, y), in tiles above the
/// ground: a block's height, the slope of smooth terrain, or about 0 for flat maps.
/// Nothing if no tile is there.
[[nodiscard]] std::optional<float> ground_top(const world::World& world, double x, double y,
                                              std::uint32_t ppu);
/// Bakes the light of every enabled baked Light into every Tilemap's baked_light (plan/02
/// D17): at each corner of its tiles, standing on its top, each light within reach adds
/// its colour times its falloff and how squarely it shines down, unless something taller
/// (a block, a wall on another map, a hill) stands between, as the shadows it casts; and
/// each corner is darkened where the tiles around it stand taller, as in a corner
/// between walls. Returns how many maps' bakes changed.
std::size_t bake_lighting(world::World& world, std::uint32_t ppu);
/// The baked light at map point (x, y): blended between the corners of the highest map
/// baked there; nothing (zero) where none is. Sprites and models add it, as light probes.
[[nodiscard]] std::array<float, 3> baked_light_at(const world::World& world, double x, double y,
                                                  std::uint32_t ppu);

/// How high smooth terrain stands under map point (x, y), the highest of the visible
/// smooth maps with a tile there; 0 where there is none. Sprites stand on it in the
/// Diorama style.
[[nodiscard]] double terrain_height(const world::World& world, double x, double y,
                                    std::uint32_t ppu);

/// How bright a face with `normal` is under `light`, from 0 to 1 (and above, if the light
/// adds up to more).
[[nodiscard]] float brightness(render::Vec3 normal, const DioramaLight& light) noexcept;

/// The camera's view-projection for a target of `aspect` (width / height), and its eye.
[[nodiscard]] render::Mat4 diorama_view_projection(const DioramaCamera& camera, float aspect,
                                                   render::Vec3* eye = nullptr);

/// A ray from the eye through a point of a Diorama camera's image.
struct Ray {
    render::Vec3 origin;
    render::Vec3 direction; ///< Of length 1.
};

/// The ray from `camera`'s eye through the point (`ndc_x`, `ndc_y`) of its image, where
/// x runs from -1 (left) to 1 (right) and y from -1 (bottom) to 1 (top), for a target of
/// `aspect`: what is under the cursor, for picking and dragging in the editor.
[[nodiscard]] Ray diorama_ray(const DioramaCamera& camera, float aspect, double ndc_x,
                              double ndc_y) noexcept;

/// Where `ray` meets the ground plane at `height` tiles up, as a map point (x, y); nothing
/// if it runs parallel to it or away from it.
[[nodiscard]] std::optional<std::array<double, 2>> ground_hit(const Ray& ray,
                                                              double height = 0.0) noexcept;
/// Where `ray` first meets the ground, smooth terrain's hills (terrain_height()) included,
/// as a map point (x, y): what a click on a hillside points at. As ground_hit() where
/// there is no terrain.
[[nodiscard]] std::optional<std::array<double, 2>> terrain_hit(const world::World& world,
                                                               const Ray& ray, std::uint32_t ppu);

/// The ground a Diorama camera shows around the point it looks at, relative to that point,
/// in tiles, for a target of `aspect` (width / height). The top and bottom are where the
/// screen's top and bottom edges meet the ground, so perspective shows more ground above
/// the point than below; the top is infinite when the screen's top edge looks above the
/// horizon. The sides are measured at the point, so a little more ground shows at the far
/// corners. Infinite all round for a turned camera (yaw not 0), which is never limited.
[[nodiscard]] Box diorama_view_extent(const DioramaCamera& camera, double aspect) noexcept;

/// How far from the point a Diorama camera looks at draw_diorama() draws tiles, in tiles
/// across and down (plan/02 C13): the view's extent, half again for the far corners, and
/// 16 tiles more for tall blocks and hills, but never more than max_view_tiles. So a map
/// of any size costs only the tiles around the camera.
[[nodiscard]] double diorama_draw_reach(const DioramaCamera& camera, double aspect) noexcept;
constexpr double max_view_tiles = 160.0;

/// The point lights draw_diorama() uses: every enabled Light with a Position, at its
/// height above the ground, nearest to the map point (x, y) first (ties by GUID), at most
/// render::max_point_lights of them.
[[nodiscard]] std::vector<render::PointLight> diorama_lights(const world::World& world, double x,
                                                             double y);

/// Draws the world in the Diorama style: tilemaps as ground and blocks, sprites as
/// upright billboards facing the camera, ordered by depth, lit and fogged. Missing images
/// and invalid tiles are counted as in draw_world(); `drawn` counts quads.
DrawStats draw_diorama(const world::World& world, TextureCache& textures,
                       render::WorldRenderer& renderer, render::RenderTarget& target,
                       const DioramaCamera& camera, render::Color clear,
                       const DioramaLight& light = {}, render::Fog fog = {});

} // namespace engine::scene2d
