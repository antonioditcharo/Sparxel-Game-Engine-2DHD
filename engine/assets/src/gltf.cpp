#include "engine/assets/gltf.h"

#include "engine/serialize/json.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>

namespace engine::assets {

namespace {

using Json = serialize::Json;
/// A 4 x 4 matrix, column by column, as glTF writes them.
using Matrix = std::array<float, 16>;

constexpr Matrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
/// More than any model of a 2D game needs: refused beyond it.
constexpr std::size_t most_vertices = 3'000'000;

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t r = 0; r < 4; ++r) {
            float sum = 0.0F;
            for (std::size_t k = 0; k < 4; ++k) {
                sum += a.at((k * 4) + r) * b.at((c * 4) + k);
            }
            out.at((c * 4) + r) = sum;
        }
    }
    return out;
}

std::array<float, 3> apply(const Matrix& m, const std::array<float, 3>& p, float w) {
    return {(m[0] * p[0]) + (m[4] * p[1]) + (m[8] * p[2]) + (m[12] * w),
            (m[1] * p[0]) + (m[5] * p[1]) + (m[9] * p[2]) + (m[13] * w),
            (m[2] * p[0]) + (m[6] * p[1]) + (m[10] * p[2]) + (m[14] * w)};
}

std::array<float, 3> normalized(std::array<float, 3> v) {
    const float length = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
    if (!(length > 0.0F) || !std::isfinite(length)) {
        return {0.0F, 1.0F, 0.0F};
    }
    return {v[0] / length, v[1] / length, v[2] / length};
}

/// A node's own transform: its matrix, or translation, rotation and scale.
Matrix local_of(const Json& node) {
    const auto numbers = [&](const char* key,
                             std::size_t count) -> std::optional<std::vector<float>> {
        const auto found = node.find(key);
        if (found == node.end() || !found->is_array() || found->size() != count) {
            return std::nullopt;
        }
        std::vector<float> out;
        for (const Json& v : *found) {
            if (!v.is_number()) {
                return std::nullopt;
            }
            out.push_back(v.get<float>());
        }
        return out;
    };
    if (const auto m = numbers("matrix", 16)) {
        Matrix out{};
        std::copy(m->begin(), m->end(), out.begin());
        return out;
    }
    const std::vector<float> t = numbers("translation", 3).value_or(std::vector<float>{0, 0, 0});
    const std::vector<float> q = numbers("rotation", 4).value_or(std::vector<float>{0, 0, 0, 1});
    const std::vector<float> s = numbers("scale", 3).value_or(std::vector<float>{1, 1, 1});
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    // Rotation from the quaternion, then scaled, then moved.
    return Matrix{(1 - (2 * ((y * y) + (z * z)))) * s[0],
                  (2 * ((x * y) + (z * w))) * s[0],
                  (2 * ((x * z) - (y * w))) * s[0],
                  0,
                  (2 * ((x * y) - (z * w))) * s[1],
                  (1 - (2 * ((x * x) + (z * z)))) * s[1],
                  (2 * ((y * z) + (x * w))) * s[1],
                  0,
                  (2 * ((x * z) + (y * w))) * s[2],
                  (2 * ((y * z) - (x * w))) * s[2],
                  (1 - (2 * ((x * x) + (y * y)))) * s[2],
                  0,
                  t[0],
                  t[1],
                  t[2],
                  1};
}

std::optional<std::vector<std::uint8_t>> base64(std::string_view text) {
    std::vector<std::uint8_t> out;
    std::uint32_t bits = 0;
    int count = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z') {
            value = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            value = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            value = c - '0' + 52;
        } else if (c == '+') {
            value = 62;
        } else if (c == '/') {
            value = 63;
        } else if (c == '=') {
            break;
        } else {
            return std::nullopt;
        }
        bits = (bits << 6U) | static_cast<std::uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out.push_back(
                static_cast<std::uint8_t>((bits >> static_cast<unsigned>(count)) & 0xFFU));
        }
    }
    return out;
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}

class Reader {
public:
    Reader(const Json& json, std::vector<std::vector<std::uint8_t>> buffers,
           const ModelFileReader& read)
        : json_(json), buffers_(std::move(buffers)), read_(read) {}

    /// An accessor's values as floats, `components` each; nothing if it cannot be read.
    std::optional<std::vector<float>> floats(std::int64_t index, std::size_t components,
                                             std::string& error) const {
        const Json* accessor = item("accessors", index);
        if (accessor == nullptr) {
            error = "An accessor is missing.";
            return std::nullopt;
        }
        const auto kind = accessor->value("componentType", 0);
        const auto count = accessor->value("count", std::size_t{0});
        const std::size_t size = kind == 5126 ? 4 : kind == 5123 ? 2 : kind == 5121 ? 1 : 0;
        const std::optional<std::span<const std::uint8_t>> data =
            view_of(*accessor, size * components, count, error);
        if (!data || size == 0) {
            if (size == 0) {
                error = "An accessor's numbers are of a kind not read.";
            }
            return std::nullopt;
        }
        const std::size_t stride = stride_of(*accessor, size * components);
        std::vector<float> out;
        out.reserve(count * components);
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t c = 0; c < components; ++c) {
                const std::size_t at = (i * stride) + (c * size);
                float v = 0.0F;
                if (kind == 5126) {
                    std::memcpy(&v, data->data() + at, 4);
                } else if (kind == 5123) {
                    v = static_cast<float>((*data)[at] | ((*data)[at + 1] << 8U)) / 65535.0F;
                } else {
                    v = static_cast<float>((*data)[at]) / 255.0F;
                }
                out.push_back(std::isfinite(v) ? v : 0.0F);
            }
        }
        return out;
    }

    /// An accessor's values as whole numbers (indices).
    std::optional<std::vector<std::uint32_t>> indices(std::int64_t index,
                                                      std::string& error) const {
        const Json* accessor = item("accessors", index);
        if (accessor == nullptr) {
            error = "An accessor is missing.";
            return std::nullopt;
        }
        const auto kind = accessor->value("componentType", 0);
        const auto count = accessor->value("count", std::size_t{0});
        const std::size_t size = kind == 5125 ? 4 : kind == 5123 ? 2 : kind == 5121 ? 1 : 0;
        if (size == 0) {
            error = "Indices are of a kind not read.";
            return std::nullopt;
        }
        const std::optional<std::span<const std::uint8_t>> data =
            view_of(*accessor, size, count, error);
        if (!data) {
            return std::nullopt;
        }
        const std::size_t stride = stride_of(*accessor, size);
        std::vector<std::uint32_t> out;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t at = i * stride;
            out.push_back(size == 4 ? read_u32(*data, at)
                          : size == 2
                              ? static_cast<std::uint32_t>((*data)[at] | ((*data)[at + 1] << 8U))
                              : (*data)[at]);
        }
        return out;
    }

    /// The image of material `material`'s base colour texture, decoded once.
    std::optional<Image> texture_of(const Json& material) {
        const Json* pbr = nullptr;
        if (const auto found = material.find("pbrMetallicRoughness");
            found != material.end() && found->is_object()) {
            pbr = &*found;
        }
        if (pbr == nullptr || !pbr->contains("baseColorTexture")) {
            return std::nullopt;
        }
        const std::int64_t texture = (*pbr)["baseColorTexture"].value("index", std::int64_t{-1});
        const Json* t = item("textures", texture);
        const std::int64_t source = t != nullptr ? t->value("source", std::int64_t{-1}) : -1;
        if (const auto cached = images_.find(source); cached != images_.end()) {
            return cached->second;
        }
        const Json* image = item("images", source);
        std::optional<std::vector<std::uint8_t>> bytes;
        if (image != nullptr && image->contains("uri") && (*image)["uri"].is_string()) {
            bytes = file((*image)["uri"].get<std::string>());
        } else if (image != nullptr && image->contains("bufferView")) {
            std::string ignored;
            const Json* view = item("bufferViews", image->value("bufferView", std::int64_t{-1}));
            if (view != nullptr) {
                const std::optional<std::span<const std::uint8_t>> data =
                    whole_view(*view, ignored);
                if (data) {
                    bytes = std::vector<std::uint8_t>(data->begin(), data->end());
                }
            }
        }
        std::optional<Image> decoded;
        if (bytes) {
            std::string ignored;
            decoded = decode_png(*bytes, ignored); // PNG only; others leave the base colour
        }
        images_[source] = decoded;
        return decoded;
    }

    [[nodiscard]] const Json* item(const char* list, std::int64_t index) const {
        const auto found = json_.find(list);
        if (found == json_.end() || !found->is_array() || index < 0 ||
            static_cast<std::size_t>(index) >= found->size()) {
            return nullptr;
        }
        return &(*found)[static_cast<std::size_t>(index)];
    }

private:
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> file(const std::string& uri) const {
        if (uri.starts_with("data:")) {
            const std::size_t comma = uri.find(',');
            if (comma == std::string::npos || uri.find(";base64") == std::string::npos) {
                return std::nullopt;
            }
            return base64(std::string_view(uri).substr(comma + 1));
        }
        return read_ ? read_(uri) : std::nullopt;
    }

    [[nodiscard]] std::size_t stride_of(const Json& accessor, std::size_t element) const {
        const Json* view = item("bufferViews", accessor.value("bufferView", std::int64_t{-1}));
        const std::size_t stride = view != nullptr ? view->value("byteStride", std::size_t{0}) : 0;
        return stride > 0 ? stride : element;
    }

    std::optional<std::span<const std::uint8_t>> whole_view(const Json& view,
                                                            std::string& error) const {
        const std::int64_t buffer = view.value("buffer", std::int64_t{-1});
        if (buffer < 0 || static_cast<std::size_t>(buffer) >= buffers_.size()) {
            error = "A buffer is missing.";
            return std::nullopt;
        }
        const std::vector<std::uint8_t>& bytes = buffers_[static_cast<std::size_t>(buffer)];
        const auto offset = view.value("byteOffset", std::size_t{0});
        const auto length = view.value("byteLength", std::size_t{0});
        if (offset > bytes.size() || length > bytes.size() - offset) {
            error = "A buffer is shorter than its views say.";
            return std::nullopt;
        }
        return std::span<const std::uint8_t>(bytes).subspan(offset, length);
    }

    /// The bytes an accessor reads, checked to hold `count` elements of `element` bytes.
    std::optional<std::span<const std::uint8_t>> view_of(const Json& accessor, std::size_t element,
                                                         std::size_t count,
                                                         std::string& error) const {
        const Json* view = item("bufferViews", accessor.value("bufferView", std::int64_t{-1}));
        if (view == nullptr) {
            error = "An accessor has no buffer view.";
            return std::nullopt;
        }
        const std::optional<std::span<const std::uint8_t>> whole = whole_view(*view, error);
        if (!whole) {
            return std::nullopt;
        }
        const auto offset = accessor.value("byteOffset", std::size_t{0});
        const std::size_t stride = stride_of(accessor, element);
        const std::size_t needed = count == 0 ? 0 : ((count - 1) * stride) + element;
        if (count > most_vertices || offset > whole->size() || needed > whole->size() - offset) {
            error = "An accessor reads past its buffer.";
            return std::nullopt;
        }
        return whole->subspan(offset);
    }

    const Json& json_;
    std::vector<std::vector<std::uint8_t>> buffers_;
    const ModelFileReader& read_;
    std::map<std::int64_t, std::optional<Image>> images_;
};

} // namespace

std::optional<Model> load_gltf(std::span<const std::uint8_t> bytes, const ModelFileReader& read,
                               std::string& error) {
    // A ".glb": its JSON chunk, and its binary chunk as the first buffer.
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size()); // NOLINT
    std::optional<std::vector<std::uint8_t>> embedded;
    if (bytes.size() >= 12 && read_u32(bytes, 0) == 0x46546C67U) {
        std::size_t at = 12;
        text = {};
        while (at + 8 <= bytes.size()) {
            const std::uint32_t length = read_u32(bytes, at);
            const std::uint32_t type = read_u32(bytes, at + 4);
            if (length > bytes.size() - at - 8) {
                error = "The .glb file is cut short.";
                return std::nullopt;
            }
            const std::span<const std::uint8_t> chunk = bytes.subspan(at + 8, length);
            if (type == 0x4E4F534AU) {
                text = std::string_view(reinterpret_cast<const char*>(chunk.data()), // NOLINT
                                        chunk.size());
            } else if (type == 0x004E4942U) {
                embedded = std::vector<std::uint8_t>(chunk.begin(), chunk.end());
            }
            at += 8 + length;
        }
    }
    std::vector<serialize::Issue> issues;
    const std::optional<Json> json = serialize::parse(text, issues);
    if (!json || !json->is_object() || !json->contains("asset")) {
        error = "It is not a glTF model.";
        return std::nullopt;
    }
    if ((*json)["asset"].value("version", std::string()).rfind("2.", 0) != 0) {
        error = "Only glTF 2 models can be read.";
        return std::nullopt;
    }
    // The buffers.
    std::vector<std::vector<std::uint8_t>> buffers;
    if (const auto list = json->find("buffers"); list != json->end() && list->is_array()) {
        for (const Json& buffer : *list) {
            if (!buffer.contains("uri")) {
                buffers.push_back(embedded.value_or(std::vector<std::uint8_t>{}));
                continue;
            }
            const std::string uri = buffer.value("uri", std::string());
            std::optional<std::vector<std::uint8_t>> data;
            if (uri.starts_with("data:")) {
                const std::size_t comma = uri.find(',');
                data = comma == std::string::npos ? std::nullopt
                                                  : base64(std::string_view(uri).substr(comma + 1));
            } else if (read) {
                data = read(uri);
            }
            if (!data) {
                error = "The model's buffer " + uri + " cannot be read.";
                return std::nullopt;
            }
            buffers.push_back(std::move(*data));
        }
    }
    Reader reader(*json, std::move(buffers), read);
    Model model;
    std::size_t total = 0;
    // Each node of the scene, with its parents' transforms, drawing its mesh.
    const std::function<bool(std::int64_t, const Matrix&, int)> visit =
        [&](std::int64_t index, const Matrix& parent, int depth) -> bool {
        const Json* node = reader.item("nodes", index);
        if (node == nullptr || depth > 64) {
            return true;
        }
        const Matrix world = multiply(parent, local_of(*node));
        if (const Json* mesh = reader.item("meshes", node->value("mesh", std::int64_t{-1}))) {
            for (const Json& primitive : mesh->value("primitives", Json::array())) {
                if (primitive.value("mode", 4) != 4 || !primitive.contains("attributes")) {
                    continue; // only triangles
                }
                const Json& attributes = primitive["attributes"];
                const auto positions =
                    reader.floats(attributes.value("POSITION", std::int64_t{-1}), 3, error);
                if (!positions) {
                    return false;
                }
                std::optional<std::vector<float>> normals;
                if (attributes.contains("NORMAL")) {
                    normals = reader.floats(attributes.value("NORMAL", std::int64_t{-1}), 3, error);
                }
                std::optional<std::vector<float>> uvs;
                if (attributes.contains("TEXCOORD_0")) {
                    uvs = reader.floats(attributes.value("TEXCOORD_0", std::int64_t{-1}), 2, error);
                }
                const std::size_t count = positions->size() / 3;
                std::vector<std::uint32_t> order;
                if (primitive.contains("indices")) {
                    const auto read_indices =
                        reader.indices(primitive.value("indices", std::int64_t{-1}), error);
                    if (!read_indices) {
                        return false;
                    }
                    order = *read_indices;
                } else {
                    for (std::uint32_t i = 0; i < count; ++i) {
                        order.push_back(i);
                    }
                }
                order.resize(order.size() - (order.size() % 3));
                total += order.size();
                if (total > most_vertices) {
                    error = "The model has more than 3 million corners.";
                    return false;
                }
                ModelMesh part;
                if (const Json* material =
                        reader.item("materials", primitive.value("material", std::int64_t{-1}))) {
                    if (const auto pbr = material->find("pbrMetallicRoughness");
                        pbr != material->end() && pbr->contains("baseColorFactor") &&
                        (*pbr)["baseColorFactor"].is_array() &&
                        (*pbr)["baseColorFactor"].size() == 4) {
                        for (std::size_t c = 0; c < 4; ++c) {
                            part.color.at(c) = (*pbr)["baseColorFactor"][c].get<float>();
                        }
                    }
                    part.texture = reader.texture_of(*material);
                }
                for (std::size_t i = 0; i < order.size(); ++i) {
                    const std::size_t v = order[i];
                    if (v >= count) {
                        error = "A triangle names a corner that is not there.";
                        return false;
                    }
                    ModelVertex corner;
                    corner.position = apply(
                        world,
                        {(*positions)[v * 3], (*positions)[(v * 3) + 1], (*positions)[(v * 3) + 2]},
                        1.0F);
                    if (normals && normals->size() >= (v * 3) + 3) {
                        corner.normal = normalized(apply(
                            world,
                            {(*normals)[v * 3], (*normals)[(v * 3) + 1], (*normals)[(v * 3) + 2]},
                            0.0F));
                    }
                    if (uvs && uvs->size() >= (v * 2) + 2) {
                        corner.uv = {(*uvs)[v * 2], (*uvs)[(v * 2) + 1]};
                    }
                    part.vertices.push_back(corner);
                }
                // Without normals: each triangle's own.
                if (!normals) {
                    for (std::size_t t = 0; t + 2 < part.vertices.size(); t += 3) {
                        const auto& a = part.vertices[t].position;
                        const auto& b = part.vertices[t + 1].position;
                        const auto& c = part.vertices[t + 2].position;
                        const std::array<float, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                        const std::array<float, 3> w{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
                        const std::array<float, 3> n = normalized({(u[1] * w[2]) - (u[2] * w[1]),
                                                                   (u[2] * w[0]) - (u[0] * w[2]),
                                                                   (u[0] * w[1]) - (u[1] * w[0])});
                        for (std::size_t k = 0; k < 3; ++k) {
                            part.vertices[t + k].normal = n;
                        }
                    }
                }
                if (!part.vertices.empty()) {
                    model.meshes.push_back(std::move(part));
                }
            }
        }
        for (const Json& child : node->value("children", Json::array())) {
            if (child.is_number_integer() && !visit(child.get<std::int64_t>(), world, depth + 1)) {
                return false;
            }
        }
        return true;
    };
    const std::int64_t scene = json->value("scene", std::int64_t{0});
    const Json* roots = reader.item("scenes", scene);
    if (roots != nullptr) {
        for (const Json& node : roots->value("nodes", Json::array())) {
            if (node.is_number_integer() && !visit(node.get<std::int64_t>(), identity, 0)) {
                return std::nullopt;
            }
        }
    } else if (const auto nodes = json->find("nodes"); nodes != json->end() && nodes->is_array()) {
        for (std::size_t i = 0; i < nodes->size(); ++i) {
            if (!visit(static_cast<std::int64_t>(i), identity, 0)) {
                return std::nullopt;
            }
        }
    }
    if (model.meshes.empty()) {
        error = "The model has no triangles.";
        return std::nullopt;
    }
    model.low = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::max()};
    model.high = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest()};
    for (const ModelMesh& mesh : model.meshes) {
        for (const ModelVertex& v : mesh.vertices) {
            for (std::size_t c = 0; c < 3; ++c) {
                model.low.at(c) = std::min(model.low.at(c), v.position.at(c));
                model.high.at(c) = std::max(model.high.at(c), v.position.at(c));
            }
        }
    }
    return model;
}

} // namespace engine::assets
