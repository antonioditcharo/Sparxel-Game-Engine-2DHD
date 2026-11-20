#pragma once

// glTF 2.0 models (plan/02 C12, as Godot and every 3D tool exchange them): a ".gltf" file
// with its buffers (files beside it, or embedded as data: URIs) or a ".glb" holding all of
// it, read into triangle meshes for the Diorama style: positions, normals and texture
// coordinates, with every node's transform applied, and each material's base colour and
// PNG texture. Cameras, lights, skins and animations are not read.
//
// See docs/dev/assets.md, *Models*.

#include "engine/assets/assets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::assets {

/// One corner of a model's triangle.
struct ModelVertex {
    std::array<float, 3> position{}; ///< In the model's units (metres in most tools).
    std::array<float, 3> normal{0.0F, 1.0F, 0.0F};
    std::array<float, 2> uv{};
};

/// A part of a model with one material: triangles, three vertices each.
struct ModelMesh {
    std::vector<ModelVertex> vertices;
    std::array<float, 4> color{1.0F, 1.0F, 1.0F, 1.0F}; ///< The base colour factor.
    std::optional<Image> texture;                       ///< The base colour texture.
};

struct Model {
    std::vector<ModelMesh> meshes;
    /// The box round every vertex: its least and greatest corners.
    std::array<float, 3> low{};
    std::array<float, 3> high{};
};

/// Reads the file at `uri` beside the model (a buffer or an image); nothing if there is
/// none.
using ModelFileReader =
    std::function<std::optional<std::vector<std::uint8_t>>(std::string_view uri)>;

/// Reads a model from the bytes of a ".gltf" or ".glb" file. Nothing, with `error` in plain
/// language, if it is not one or something it needs cannot be read.
[[nodiscard]] std::optional<Model> load_gltf(std::span<const std::uint8_t> bytes,
                                             const ModelFileReader& read, std::string& error);

} // namespace engine::assets
