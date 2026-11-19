#include "engine/assets/lighting_maps.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace engine::assets {

namespace {

std::size_t index_of(const Image& image, std::uint32_t x, std::uint32_t y) {
    return ((std::size_t{y} * image.width) + x) * 4;
}

bool opaque(const Image& image, std::uint32_t x, std::uint32_t y) {
    return image.rgba[index_of(image, x, y) + 3] >= 128;
}

std::uint8_t encode(float n) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp((n * 0.5F) + 0.5F, 0.0F, 1.0F) * 255.0F));
}

} // namespace

Image make_normal_map(const Image& picture, const NormalMapSettings& settings) {
    Image out;
    out.width = picture.width;
    out.height = picture.height;
    const std::size_t count = std::size_t{picture.width} * picture.height;
    if (picture.rgba.size() != count * 4 || count == 0) {
        out.width = 0;
        out.height = 0;
        return out;
    }
    const std::uint32_t cell_w = settings.cell_width == 0 ? picture.width : settings.cell_width;
    const std::uint32_t cell_h = settings.cell_height == 0 ? picture.height : settings.cell_height;
    const std::uint32_t bevel = std::clamp<std::uint32_t>(settings.bevel, 1, 16);
    const float strength =
        std::isfinite(settings.strength) ? std::clamp(settings.strength, 0.0F, 8.0F) : 1.0F;
    const auto b = static_cast<float>(bevel);
    const bool rounded = settings.relief != Relief::Bumpy;
    const bool bumpy = settings.relief != Relief::Rounded;

    // Heights, in pixels.
    std::vector<float> height(count, 0.0F);
    for (std::uint32_t y = 0; y < picture.height; ++y) {
        const std::uint32_t cy0 = (y / cell_h) * cell_h;
        const std::uint32_t cy1 = std::min(cy0 + cell_h, picture.height);
        for (std::uint32_t x = 0; x < picture.width; ++x) {
            if (!opaque(picture, x, y)) {
                continue;
            }
            const std::uint32_t cx0 = (x / cell_w) * cell_w;
            const std::uint32_t cx1 = std::min(cx0 + cell_w, picture.width);
            float h = 0.0F;
            if (rounded) {
                // The distance to the nearest pixel outside the shape (clear, or beyond
                // the cell), from the edge between them.
                float nearest = b;
                const auto reach = static_cast<std::int64_t>(bevel) + 1;
                for (std::int64_t dy = -reach; dy <= reach; ++dy) {
                    for (std::int64_t dx = -reach; dx <= reach; ++dx) {
                        const std::int64_t sx = static_cast<std::int64_t>(x) + dx;
                        const std::int64_t sy = static_cast<std::int64_t>(y) + dy;
                        const bool outside = sx < static_cast<std::int64_t>(cx0) ||
                                             sy < static_cast<std::int64_t>(cy0) ||
                                             sx >= static_cast<std::int64_t>(cx1) ||
                                             sy >= static_cast<std::int64_t>(cy1) ||
                                             !opaque(picture, static_cast<std::uint32_t>(sx),
                                                     static_cast<std::uint32_t>(sy));
                        if (outside) {
                            const auto fx = static_cast<float>(dx);
                            const auto fy = static_cast<float>(dy);
                            nearest = std::min(nearest, std::sqrt((fx * fx) + (fy * fy)) - 0.5F);
                        }
                    }
                }
                // A quarter circle of radius `bevel`: steep at the edge, flat on top.
                const float t = b - std::clamp(nearest, 0.0F, b);
                h += std::sqrt(std::max((b * b) - (t * t), 0.0F));
            }
            if (bumpy) {
                const std::size_t i = index_of(picture, x, y);
                const float bright =
                    static_cast<float>(
                        std::max({picture.rgba[i], picture.rgba[i + 1], picture.rgba[i + 2]})) /
                    255.0F;
                h += bright * 2.0F;
            }
            height[(std::size_t{y} * picture.width) + x] = h;
        }
    }

    out.rgba.assign(count * 4, 255);
    for (std::uint32_t y = 0; y < picture.height; ++y) {
        const std::uint32_t cy0 = (y / cell_h) * cell_h;
        const std::uint32_t cy1 = std::min(cy0 + cell_h, picture.height);
        for (std::uint32_t x = 0; x < picture.width; ++x) {
            const std::size_t o = index_of(picture, x, y);
            if (!opaque(picture, x, y)) {
                out.rgba[o] = 128;
                out.rgba[o + 1] = 128;
                continue; // blue and alpha 255: flat
            }
            const std::uint32_t cx0 = (x / cell_w) * cell_w;
            const std::uint32_t cx1 = std::min(cx0 + cell_w, picture.width);
            const auto at = [&](std::uint32_t px, std::uint32_t py) {
                return height[(std::size_t{py} * picture.width) + px];
            };
            const float here = at(x, y);
            // Within the cell: beyond its edge, the slope stops.
            const float left = x > cx0 ? at(x - 1, y) : here;
            const float right = x + 1 < cx1 ? at(x + 1, y) : here;
            const float up = y > cy0 ? at(x, y - 1) : here;
            const float down = y + 1 < cy1 ? at(x, y + 1) : here;
            // Rising to the right tilts it left; rising down the picture tilts it up.
            const float nx = -(right - left) * 0.5F * strength;
            const float ny = (down - up) * 0.5F * strength;
            const float length = std::sqrt((nx * nx) + (ny * ny) + 1.0F);
            out.rgba[o] = encode(nx / length);
            out.rgba[o + 1] = encode(ny / length);
            out.rgba[o + 2] = encode(1.0F / length);
        }
    }
    return out;
}

Image make_glow_map(const Image& picture, float threshold) {
    Image out;
    out.width = picture.width;
    out.height = picture.height;
    const std::size_t count = std::size_t{picture.width} * picture.height;
    if (picture.rgba.size() != count * 4) {
        out.width = 0;
        out.height = 0;
        return out;
    }
    const float least = std::isfinite(threshold) ? std::clamp(threshold, 0.0F, 1.0F) : 1.0F;
    out.rgba.assign(count * 4, 0);
    for (std::size_t i = 0; i < count * 4; i += 4) {
        const float bright = static_cast<float>(std::max(
                                 {picture.rgba[i], picture.rgba[i + 1], picture.rgba[i + 2]})) /
                             255.0F;
        if (picture.rgba[i + 3] >= 128 && bright >= least) {
            out.rgba[i] = picture.rgba[i];
            out.rgba[i + 1] = picture.rgba[i + 1];
            out.rgba[i + 2] = picture.rgba[i + 2];
            out.rgba[i + 3] = 255;
        }
    }
    return out;
}

} // namespace engine::assets
