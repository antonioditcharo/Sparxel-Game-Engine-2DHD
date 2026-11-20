#include "engine/game/combat.h"

#include "engine/game/behaviours.h"
#include "engine/game/logic_world.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace engine::game {

namespace {

/// Knockback lasts this long, whatever its distance.
constexpr double push_seconds = 0.15;
/// A character seen from the side hops this high when knocked back, in tiles.
constexpr double knock_hop = 0.5;
/// The blink: shown, then faint, each this long.
constexpr double blink_seconds = 0.08;
/// A hurt's white flash, fading out over this long.
constexpr double flash_seconds = 0.12;
/// The defeated dissolve away over this long, then are hidden.
constexpr double fade_seconds = 0.4;

bool finite_positive(double value) {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

bool Health::take(double amount) {
    if (defeated || !std::isfinite(amount) || amount <= 0.0) {
        return false;
    }
    health = std::max(0.0, (std::isfinite(health) ? health : 0.0) - amount);
    if (health <= 0.0) {
        defeated = true;
        return true;
    }
    return false;
}

void Health::hurt(double amount) {
    static_cast<void>(take(amount));
}

void Health::heal(double amount) {
    if (defeated || !std::isfinite(amount) || amount <= 0.0) {
        return;
    }
    const double most = std::isfinite(max_health) ? std::max(max_health, 0.0) : 0.0;
    health = std::min(most, (std::isfinite(health) ? health : 0.0) + amount);
}

void Health::revive() {
    defeated = false;
    health = std::isfinite(max_health) ? std::max(max_health, 0.0) : 0.0;
    safe = 0.0;
    push_x = 0.0;
    push_y = 0.0;
    push_time = 0.0;
}

void register_combat(reflect::Registry& registry) {
    auto health =
        registry.add_type<Health>("health").display_name("Health").category("Combat").tooltip(
            "What hit boxes take away. Hurt, it blinks for a moment and is "
            "knocked back; at 0 it is defeated.");
    health.property<&Health::health>("health")
        .range(0.0, 10000.0)
        .step(1.0)
        .tooltip("What it has now.");
    health.property<&Health::max_health>("max_health")
        .range(0.0, 10000.0)
        .step(1.0)
        .display_name("Max health")
        .tooltip("As much as healing gives back.");
    health.property<&Health::damage>("damage")
        .range(0.0, 10000.0)
        .step(1.0)
        .tooltip("What its own hit boxes take from others.");
    health.property<&Health::hurt_by>("hurt_by").display_name("Hurt by").tooltip(
        "Tags of the objects whose hits hurt it, \"enemy\" for the hero; empty for any "
        "hit box.");
    health.property<&Health::invulnerable>("invulnerable")
        .range(0.0, 10.0)
        .step(0.1)
        .unit(reflect::Unit::Seconds)
        .display_name("Safe after a hurt")
        .tooltip("How long hits do nothing after one hurt it; it blinks meanwhile.");
    health.property<&Health::knockback>("knockback")
        .range(0.0, 16.0)
        .step(0.25)
        .unit(reflect::Unit::Tiles)
        .tooltip("How far it is pushed away from what hit it.");
    health.property<&Health::hide_when_defeated>("hide_when_defeated")
        .display_name("Hide when defeated")
        .tooltip("Off, it stays to be seen (fallen), still and harmless.");
    health.property<&Health::defeated>("beaten").flags(reflect::property_flag::hidden);
    health.property<&Health::safe>("safe").flags(reflect::property_flag::hidden);
    health.property<&Health::push_x>("push_x").flags(reflect::property_flag::hidden);
    health.property<&Health::push_y>("push_y").flags(reflect::property_flag::hidden);
    health.property<&Health::push_time>("push_time").flags(reflect::property_flag::hidden);
    health.property<&Health::shown_alpha>("shown_alpha").flags(reflect::property_flag::hidden);
    health.property<&Health::hid>("hid").flags(reflect::property_flag::hidden);
    health.property<&Health::flash_time>("flash_time").flags(reflect::property_flag::hidden);
    health.property<&Health::fading>("fading").flags(reflect::property_flag::hidden);
    health.action<&Health::hurt>("take_health", {"amount"})
        .text("Hurt it by {amount}")
        .tooltip("Takes health away, as a hit would (but without knockback).");
    health.action<&Health::heal>("heal", {"amount"})
        .text("Heal it by {amount}")
        .tooltip("Gives health back, up to its most.");
    health.action<&Health::revive>("revive")
        .text("Revive it")
        .tooltip("Back to full health, and shown again.");
    health.event<double, std::string>("hurt", {"amount", "who"})
        .text("When it is hurt by {who}")
        .tooltip("A hit took health away; {amount} is how much.");
    health.event<std::string>("defeated", {"who"})
        .text("When it is defeated by {who}")
        .tooltip("Its health reached 0.");
}

bool register_combat_components(world::World& world) {
    return world.register_component<Health>();
}

std::vector<std::string> parse_tag_list(std::string_view text) {
    std::vector<std::string> tags;
    std::string word;
    const auto finish = [&] {
        if (!word.empty() && std::find(tags.begin(), tags.end(), word) == tags.end()) {
            tags.push_back(word);
        }
        word.clear();
    };
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 || c == '_') {
            word.push_back(static_cast<char>(std::tolower(u)));
        } else {
            finish();
        }
    }
    finish();
    return tags;
}

std::vector<Hurt> apply_hits(world::World& world, std::span<const scene2d::HitContact> started) {
    std::vector<Hurt> hurts;
    for (const scene2d::HitContact& contact : started) {
        Health* target = world.get<Health>(contact.target);
        if (target == nullptr || target->defeated || target->safe > 0.0) {
            continue;
        }
        const Health* attacker = world.get<Health>(contact.attacker);
        if (attacker != nullptr && attacker->defeated) {
            continue;
        }
        const std::vector<std::string> wanted = parse_tag_list(target->hurt_by);
        if (!wanted.empty()) {
            const std::vector<std::string> has = tags_of(world, contact.attacker);
            const bool matches =
                std::any_of(wanted.begin(), wanted.end(), [&](const std::string& t) {
                    return std::find(has.begin(), has.end(), t) != has.end();
                });
            if (!matches) {
                continue;
            }
        }
        const double amount =
            attacker == nullptr
                ? 1.0
                : (std::isfinite(attacker->damage) ? std::max(attacker->damage, 0.0) : 0.0);
        if (amount <= 0.0) {
            continue;
        }
        const Hurt hurt{contact.target, contact.attacker, amount, target->take(amount)};
        target->flash_time = flash_seconds;
        // Safe for a while, blinking from its alpha now.
        if (finite_positive(target->invulnerable)) {
            target->safe = target->invulnerable;
            if (const scene2d::Sprite* sprite = world.get<scene2d::Sprite>(contact.target)) {
                target->shown_alpha = sprite->tint_a;
            }
        }
        // Knocked away from the attacker (sideways only for one seen from the side, with a
        // hop).
        const scene2d::Transform2D* from = world.get<scene2d::Transform2D>(contact.attacker);
        const scene2d::Transform2D* to = world.get<scene2d::Transform2D>(contact.target);
        if (!hurt.defeated && finite_positive(target->knockback) && from != nullptr &&
            to != nullptr) {
            Jumping* jumping = world.get<Jumping>(contact.target);
            double dx = to->x - from->x;
            double dy = jumping != nullptr ? 0.0 : to->y - from->y;
            if (std::hypot(dx, dy) < 1e-6) {
                // On top of each other: backwards, away from where it faces.
                const TopDownMover* mover = world.get<TopDownMover>(contact.target);
                const Facing facing = mover != nullptr ? mover->facing : Facing::Right;
                dx = facing == Facing::Left ? 1.0 : facing == Facing::Right ? -1.0 : 0.0;
                dy = jumping != nullptr       ? 0.0
                     : facing == Facing::Up   ? 1.0
                     : facing == Facing::Down ? -1.0
                                              : 0.0;
                if (dx == 0.0 && dy == 0.0) {
                    dx = -1.0; // seen from the side, facing up or down
                }
            }
            const double length = std::hypot(dx, dy);
            const double speed = std::min(target->knockback, 16.0) / push_seconds;
            target->push_x = dx / length * speed;
            target->push_y = dy / length * speed;
            target->push_time = push_seconds;
            if (jumping != nullptr && finite_positive(jumping->gravity)) {
                jumping->fall_speed = -std::sqrt(2.0 * jumping->gravity * knock_hop);
                jumping->on_ground = false;
            }
        }
        hurts.push_back(hurt);
    }
    return hurts;
}

void update_health(world::World& world, double seconds, std::uint32_t ppu, bool flashes) {
    const reflect::TypeInfo* type = world.registry().find_type<Health>();
    if (type == nullptr || !std::isfinite(seconds) || seconds < 0.0) {
        return;
    }
    // Solid Bodies where they stand, to be knocked against.
    struct Obstacle {
        world::Entity entity;
        scene2d::Box box;
        const Body* body;
    };
    std::vector<Obstacle> obstacles;
    if (const reflect::TypeInfo* body_type = world.registry().find_type<Body>()) {
        world.each(*body_type, [&](world::Entity entity, reflect::ObjectRef ref) {
            const Body* body = ref.as<Body>();
            const scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            if (body != nullptr && at != nullptr && body->enabled) {
                obstacles.push_back(
                    Obstacle{entity, feet_box(at->x, at->y, body->width, body->height), body});
            }
        });
    }
    world.each(*type, [&](world::Entity entity, reflect::ObjectRef ref) {
        Health* health = ref.as<Health>();
        if (health == nullptr) {
            return;
        }
        scene2d::Sprite* sprite = world.get<scene2d::Sprite>(entity);
        // The hurt's flash, fading.
        if (health->flash_time > 0.0) {
            health->flash_time = std::max(0.0, health->flash_time - seconds);
            if (sprite != nullptr) {
                sprite->flash = flashes ? health->flash_time / flash_seconds : 0.0;
            }
        }
        // Defeated: dissolving, then hidden (once, so logic may show it again); still,
        // blinking no more.
        if (health->defeated) {
            if (health->safe > 0.0 && sprite != nullptr) {
                sprite->tint_a = health->shown_alpha;
            }
            health->safe = 0.0;
            health->push_time = 0.0;
            if (health->hide_when_defeated && !health->hid && sprite != nullptr) {
                health->fading += seconds;
                sprite->dissolve = std::min(1.0, health->fading / fade_seconds);
                if (health->fading >= fade_seconds - 1e-9) {
                    sprite->visible = false;
                    sprite->dissolve = 0.0;
                    health->hid = true;
                    health->fading = 0.0;
                }
            }
            return;
        }
        if (health->fading > 0.0) {
            // Revived while dissolving: whole again.
            health->fading = 0.0;
            if (sprite != nullptr) {
                sprite->dissolve = 0.0;
            }
        }
        if (health->hid) {
            // Revived after being hidden: shown again.
            health->hid = false;
            if (sprite != nullptr) {
                sprite->visible = true;
            }
        }
        // Blinking while safe; its alpha back after.
        if (health->safe > 0.0) {
            health->safe = std::max(0.0, health->safe - seconds);
            if (sprite != nullptr) {
                const bool faint =
                    flashes && health->safe > 0.0 &&
                    static_cast<std::int64_t>(std::floor(health->safe / blink_seconds)) % 2 == 0;
                sprite->tint_a = faint ? static_cast<std::uint8_t>(health->shown_alpha / 4)
                                       : health->shown_alpha;
            }
        }
        // Knockback, as far as walls let it.
        if (health->push_time > 0.0) {
            const double step = std::min(seconds, health->push_time);
            health->push_time -= seconds;
            if (health->push_time < 1e-9) {
                health->push_time = 0.0; // not a rounding error left over
            }
            scene2d::Transform2D* at = world.get<scene2d::Transform2D>(entity);
            const TopDownMover* mover = world.get<TopDownMover>(entity);
            if (at == nullptr || !std::isfinite(health->push_x) || !std::isfinite(health->push_y)) {
                return;
            }
            std::vector<scene2d::Box> others;
            const std::vector<std::string> tags = tags_of(world, entity);
            for (const Obstacle& obstacle : obstacles) {
                if (obstacle.entity != entity && body_blocks(*obstacle.body, tags)) {
                    others.push_back(obstacle.box);
                }
            }
            for (const scene2d::Axis axis : {scene2d::Axis::X, scene2d::Axis::Y}) {
                const double wanted =
                    (axis == scene2d::Axis::X ? health->push_x : health->push_y) * step;
                if (wanted == 0.0) {
                    continue;
                }
                double allowed = wanted;
                if (mover != nullptr) {
                    const scene2d::Box box = hitbox(*mover, at->x, at->y);
                    allowed = scene2d::sweep_boxes(
                        box, axis, scene2d::sweep(world, box, axis, wanted, ppu), others);
                }
                (axis == scene2d::Axis::X ? at->x : at->y) += allowed;
            }
        }
    });
}

} // namespace engine::game
