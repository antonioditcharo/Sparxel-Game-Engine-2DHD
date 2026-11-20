// glTF models: triangles read with their nodes' transforms, materials, and every way
// buffers come.

#include "engine/assets/gltf.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace assets = engine::assets;

namespace {

std::string base64(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const std::uint32_t v = (bytes[i] << 16U) | (bytes[i + 1] << 8U) | bytes[i + 2];
        out += digits[(v >> 18U) & 63U];
        out += digits[(v >> 12U) & 63U];
        out += digits[(v >> 6U) & 63U];
        out += digits[v & 63U];
    }
    if (i + 1 == bytes.size()) {
        const std::uint32_t v = bytes[i] << 16U;
        out += digits[(v >> 18U) & 63U];
        out += digits[(v >> 12U) & 63U];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const std::uint32_t v = (bytes[i] << 16U) | (bytes[i + 1] << 8U);
        out += digits[(v >> 18U) & 63U];
        out += digits[(v >> 12U) & 63U];
        out += digits[(v >> 6U) & 63U];
        out += '=';
    }
    return out;
}

/// A triangle: three positions, then indices 0, 1, 2 as unsigned shorts.
std::vector<std::uint8_t> triangle_buffer() {
    const float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::uint16_t indices[3] = {0, 1, 2};
    std::vector<std::uint8_t> bytes(sizeof positions + sizeof indices);
    std::memcpy(bytes.data(), positions, sizeof positions);
    std::memcpy(bytes.data() + sizeof positions, indices, sizeof indices);
    return bytes;
}

std::string triangle_json(const std::string& buffer) {
    return R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"translation": [1, 2, 3], "children": [1]}, {"mesh": 0, "scale": [2, 2, 2]}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0.25, 1]}}],
        "buffers": [)" +
           buffer + R"(],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                        {"buffer": 0, "byteOffset": 36, "byteLength": 6}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                      {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}]})";
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return {text.begin(), text.end()};
}

void check_triangle(const assets::Model& model) {
    REQUIRE(model.meshes.size() == 1);
    const assets::ModelMesh& mesh = model.meshes[0];
    REQUIRE(mesh.vertices.size() == 3);
    // Scaled by 2 under the child, then moved by (1, 2, 3) by the parent.
    CHECK(mesh.vertices[0].position == std::array<float, 3>{1, 2, 3});
    CHECK(mesh.vertices[1].position == std::array<float, 3>{3, 2, 3});
    CHECK(mesh.vertices[2].position == std::array<float, 3>{1, 4, 3});
    // No normals given: the triangle's own, facing +z.
    CHECK(mesh.vertices[0].normal == std::array<float, 3>{0, 0, 1});
    CHECK(mesh.color == std::array<float, 4>{1, 0.5F, 0.25F, 1});
    CHECK_FALSE(mesh.texture.has_value());
    CHECK(model.low == std::array<float, 3>{1, 2, 3});
    CHECK(model.high == std::array<float, 3>{3, 4, 3});
}

} // namespace

TEST_CASE("glTF: a triangle from a buffer in the file, beside it, or in a .glb") {
    std::string error;
    // Embedded as a data: URI.
    const std::string embedded =
        triangle_json(R"({"byteLength": 42, "uri": "data:application/octet-stream;base64,)" +
                      base64(triangle_buffer()) + R"("})");
    const std::optional<assets::Model> model =
        assets::load_gltf(bytes_of(embedded), nullptr, error);
    REQUIRE(model.has_value());
    check_triangle(model.value_or(assets::Model{}));
    // In a file beside it.
    const std::string beside = triangle_json(R"({"byteLength": 42, "uri": "triangle.bin"})");
    const assets::ModelFileReader read =
        [](std::string_view uri) -> std::optional<std::vector<std::uint8_t>> {
        if (uri == "triangle.bin") {
            return triangle_buffer();
        }
        return std::nullopt;
    };
    const std::optional<assets::Model> read_beside =
        assets::load_gltf(bytes_of(beside), read, error);
    REQUIRE(read_beside.has_value());
    check_triangle(read_beside.value_or(assets::Model{}));
    CHECK_FALSE(assets::load_gltf(bytes_of(beside), nullptr, error).has_value());
    CHECK(error == "The model's buffer triangle.bin cannot be read.");
    // A .glb: the JSON and the buffer in chunks.
    std::string json = triangle_json(R"({"byteLength": 42})");
    while (json.size() % 4 != 0) {
        json += ' ';
    }
    std::vector<std::uint8_t> bin = triangle_buffer();
    while (bin.size() % 4 != 0) {
        bin.push_back(0);
    }
    std::vector<std::uint8_t> glb;
    const auto u32 = [&glb](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            glb.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    };
    u32(0x46546C67U);
    u32(2);
    u32(static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + bin.size()));
    u32(static_cast<std::uint32_t>(json.size()));
    u32(0x4E4F534AU);
    glb.insert(glb.end(), json.begin(), json.end());
    u32(static_cast<std::uint32_t>(bin.size()));
    u32(0x004E4942U);
    glb.insert(glb.end(), bin.begin(), bin.end());
    const std::optional<assets::Model> from_glb = assets::load_gltf(glb, nullptr, error);
    REQUIRE(from_glb.has_value());
    check_triangle(from_glb.value_or(assets::Model{}));
}

TEST_CASE("glTF: what cannot be read is said") {
    std::string error;
    CHECK_FALSE(assets::load_gltf(bytes_of("not json"), nullptr, error).has_value());
    CHECK(error == "It is not a glTF model.");
    CHECK_FALSE(assets::load_gltf(bytes_of(R"({"asset": {"version": "1.0"}})"), nullptr, error)
                    .has_value());
    CHECK(error == "Only glTF 2 models can be read.");
    CHECK_FALSE(
        assets::load_gltf(bytes_of(R"({"asset": {"version": "2.0"}, "nodes": []})"), nullptr, error)
            .has_value());
    CHECK(error == "The model has no triangles.");
    // An accessor reading past its buffer.
    const std::string bad =
        triangle_json(R"({"byteLength": 42, "uri": "data:application/octet-stream;base64,)" +
                      base64(std::vector<std::uint8_t>(10, 0)) + R"("})");
    CHECK_FALSE(assets::load_gltf(bytes_of(bad), nullptr, error).has_value());
    CHECK(error == "A buffer is shorter than its views say.");
}
