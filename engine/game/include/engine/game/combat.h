#pragma once

// Combat for action games: health that hit boxes take away, a moment of safety after each
// hurt (the character blinks), being knocked back, and being defeated. The hit and hurt
// boxes come from Sprite Studio (per frame, scene2d's hit_contacts()); this decides what a
// contact does. Rules hear "hurt" and "defeated", and can set health like any property.
//
// See docs/dev/game.md, *Health and combat*.

#include "engine/foundation/reflect.h"
#include "engine/scene2d/scene2d.h"
#include "engine/world/world.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::game {

/// "Health": what hit boxes take away. A hit box of another object on this one's hurt box
/// hurts it by that object's `damage` (1 for an object with no Health: spikes, a
/// fireball), unless it is safe from the last hurt or the attacker is not one it is hurt
/// by. Hurt, it blinks while safe and is knocked away from the attacker; at 0 it is
/// defeated: hidden (if `hide_when_defeated`), it stops moving and no longer hits or is hit.
struct Health {
    double health = 3.0;
    double max_health = 3.0;
    double damage = 1.0;       ///< What its own hit boxes take from others.
    std::string hurt_by;       ///< Tags whose hits hurt it ("enemy"); empty: any hit box.
    double invulnerable = 0.8; ///< Seconds after a hurt that hits do nothing (it blinks).
    double knockback = 1.0;    ///< Tiles it is pushed away from what hit it.
    bool hide_when_defeated = true;
    // Hidden, saved with the scene and games.
    bool defeated = false;
    double safe = 0.0;   ///< Seconds still safe from hits.
    double push_x = 0.0; ///< Knockback still to come, in tiles per second...
    double push_y = 0.0;
    double push_time = 0.0;         ///< ...for these seconds; it cannot walk meanwhile.
    std::uint8_t shown_alpha = 255; ///< Its sprite's alpha before blinking.
    bool hid = false;               ///< Its sprite was hidden on being defeated.

    /// Takes `amount` (above 0) and defeats it at 0; nothing if already defeated. Returns
    /// whether this defeated it.
    bool take(double amount);
    /// take(), for logic.
    void hurt(double amount);
    /// Gives back `amount`, up to max_health; nothing if defeated.
    void heal(double amount);
    /// Back to full health and shown again.
    void revive();
};

/// A hurt that a hit did, for the runtime's "hurt" and "defeated" events.
struct Hurt {
    world::Entity target;
    world::Entity attacker;
    double amount = 0.0;
    bool defeated = false;
};

void register_combat(reflect::Registry& registry);
[[nodiscard]] bool register_combat_components(world::World& world);

/// The tags in `text` ("enemy, flying"): lower case, each once, in order.
[[nodiscard]] std::vector<std::string> parse_tag_list(std::string_view text);

/// What the contacts that began this step do: each target with Health, not defeated and
/// not safe, whose `hurt_by` the attacker matches, is hurt, made safe for its
/// `invulnerable` seconds, and knocked back (a character seen from the side hops a little
/// too). A defeated attacker hurts nothing. In the contacts' order.
std::vector<Hurt> apply_hits(world::World& world, std::span<const scene2d::HitContact> started);

/// One step of `seconds`: safety running out (the sprite blinks meanwhile, and shows again
/// after), knockback moving each one as far as solid tiles and Solid Bodies let it, and
/// defeated ones kept hidden.
void update_health(world::World& world, double seconds, std::uint32_t ppu);

} // namespace engine::game
