#include "editor/core/asset_packs.h"

#include "engine/assets/assets.h"
#include "engine/serialize/json.h"

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>
#include <utility>

#include "editor/core/asset_tools.h"

namespace editor {

namespace fs = std::filesystem;
namespace assets = engine::assets;
namespace serialize = engine::serialize;

namespace {

constexpr std::string_view meta = ".meta";

/// Files whose text may name other assets.
bool is_text(std::string_view path) {
    constexpr std::array<std::string_view, 9> kinds{".dscene",  ".dprefab", ".drules",
                                                    ".dtalk",   ".dtable",  ".sheet",
                                                    ".dsprite", ".json",    ".txt"};
    return std::ranges::any_of(kinds, [&](std::string_view k) { return path.ends_with(k); });
}

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_file(const fs::path& path, std::string_view bytes) {
    std::error_code failed;
    fs::create_directories(path.parent_path(), failed);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return !failed && out.good();
}

fs::path native(std::string_view relative) {
    return fs::path(std::u8string(relative.begin(), relative.end()));
}

bool safe_relative(std::string_view path) {
    return !path.empty() && path.find("..") == std::string_view::npos && path.front() != '/' &&
           path.find(':') == std::string_view::npos && path.find('\\') == std::string_view::npos;
}

} // namespace

std::vector<std::string> pack_contents(const fs::path& project,
                                       const std::vector<std::string>& chosen) {
    assets::AssetDatabase database(project / "assets");
    static_cast<void>(database.refresh());
    const std::vector<assets::AssetRecord> records = database.assets();
    std::set<std::string> known;
    for (const assets::AssetRecord& record : records) {
        known.insert(record.path);
    }
    std::deque<std::string> todo;
    for (const std::string& pick : chosen) {
        if (known.contains(pick)) {
            todo.push_back(pick);
            continue;
        }
        // A folder: everything in it.
        const std::string prefix = pick.empty() || pick.ends_with('/') ? pick : pick + "/";
        for (const assets::AssetRecord& record : records) {
            if (record.path.starts_with(prefix)) {
                todo.push_back(record.path);
            }
        }
    }
    std::set<std::string> packed;
    while (!todo.empty()) {
        const std::string path = todo.front();
        todo.pop_front();
        if (!known.contains(path) || !packed.insert(path).second) {
            continue;
        }
        for (const std::string& member : asset_group(project, path)) {
            todo.push_back(member);
        }
        if (!is_text(path)) {
            continue;
        }
        const std::optional<std::string> text = read_file(project / "assets" / native(path));
        if (!text) {
            continue;
        }
        // What it names: by GUID (pictures, sounds), or by path (scenes, logic, tables
        // without their ".dtable").
        for (const assets::AssetRecord& record : records) {
            if (record.path == path || packed.contains(record.path)) {
                continue;
            }
            const std::string_view table =
                record.path.ends_with(".dtable")
                    ? std::string_view(record.path).substr(0, record.path.size() - 7)
                    : std::string_view{};
            if (text->find(record.guid.to_string()) != std::string::npos ||
                text->find(record.path) != std::string::npos ||
                (!table.empty() && text->find(table) != std::string::npos)) {
                todo.push_back(record.path);
            }
        }
    }
    return {packed.begin(), packed.end()};
}

std::optional<std::vector<std::string>> export_pack(const fs::path& project,
                                                    const std::vector<std::string>& chosen,
                                                    const fs::path& folder, std::string_view name,
                                                    std::string& error) {
    std::error_code failed;
    if (fs::exists(folder, failed) &&
        (!fs::is_directory(folder, failed) ||
         fs::directory_iterator(folder, failed) != fs::directory_iterator())) {
        error = "That folder already has something in it: choose an empty or a new one.";
        return std::nullopt;
    }
    std::vector<std::string> files = pack_contents(project, chosen);
    if (files.empty()) {
        error = "There is nothing to pack: choose files or folders of assets.";
        return std::nullopt;
    }
    serialize::Json listed = serialize::Json::array();
    for (const std::string& file : files) {
        const fs::path from = project / "assets" / native(file);
        const fs::path to = folder / "assets" / native(file);
        fs::create_directories(to.parent_path(), failed);
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, failed);
        fs::path meta_from = from;
        meta_from += native(meta);
        fs::path meta_to = to;
        meta_to += native(meta);
        if (!failed && fs::exists(meta_from, failed)) {
            fs::copy_file(meta_from, meta_to, fs::copy_options::overwrite_existing, failed);
        }
        if (failed) {
            error = "The pack could not be written: " + failed.message();
            return std::nullopt;
        }
        listed.push_back(file);
    }
    const serialize::Json about{{"format", std::string(pack_format)},
                                {"version", pack_version},
                                {"name", std::string(name)},
                                {"files", std::move(listed)}};
    if (!write_file(folder / "pack.json", serialize::to_text(about))) {
        error = "The pack could not be written.";
        return std::nullopt;
    }
    return files;
}

std::optional<PackImport> import_pack(const fs::path& project, const fs::path& pack,
                                      std::string& error) {
    const std::optional<std::string> text = read_file(pack / "pack.json");
    if (!text) {
        error = "That folder is not an asset pack: it has no pack.json.";
        return std::nullopt;
    }
    std::vector<serialize::Issue> issues;
    const std::optional<serialize::Json> about = serialize::parse(*text, issues);
    if (!about || !about->is_object() || about->value("format", std::string()) != pack_format ||
        !about->contains("files") || !(*about)["files"].is_array()) {
        error = "That folder is not an asset pack Sparxel can read.";
        return std::nullopt;
    }
    PackImport result;
    result.name = about->value("name", std::string());
    assets::AssetDatabase database(project / "assets");
    static_cast<void>(database.refresh());
    std::vector<std::string> problems;
    std::vector<std::string> files;
    // The pack's GUIDs of files the project has already under its own: rewritten in what
    // is brought in, so it uses the project's.
    std::map<std::string, std::string> same_as;
    for (const serialize::Json& entry : (*about)["files"]) {
        if (!entry.is_string() || !safe_relative(entry.get<std::string>())) {
            error = "The pack names a file outside its own folder.";
            return std::nullopt;
        }
        const std::string file = entry.get<std::string>();
        const fs::path from = pack / "assets" / native(file);
        const fs::path to = project / "assets" / native(file);
        const std::optional<std::string> bytes = read_file(from);
        if (!bytes) {
            error = "The pack is missing " + file + ".";
            return std::nullopt;
        }
        // Its GUID, from its ".meta", if it has one.
        fs::path meta_from = from;
        meta_from += native(meta);
        std::string guid;
        if (const std::optional<std::string> meta_text = read_file(meta_from)) {
            std::vector<serialize::Issue> ignored;
            if (const std::optional<serialize::Json> m = serialize::parse(*meta_text, ignored);
                m && m->is_object()) {
                guid = m->value("guid", std::string());
            }
        }
        std::error_code failed;
        if (fs::exists(to, failed)) {
            if (read_file(to) == bytes) {
                result.already.push_back(file);
                const assets::AssetRecord* own = database.find(file);
                if (own != nullptr && !guid.empty() && own->guid.to_string() != guid) {
                    same_as[guid] = own->guid.to_string();
                }
            } else {
                problems.push_back(file + " is in the project already, and different");
            }
            continue;
        }
        if (const std::optional<engine::Guid> id = engine::Guid::parse(guid)) {
            if (const assets::AssetRecord* other = database.find(*id)) {
                // Moved in the project: a picture or sound is found by its GUID wherever it
                // is; what is named by its path is not.
                if (is_text(file)) {
                    problems.push_back(file + " is in the project already, as " + other->path);
                } else {
                    result.already.push_back(file);
                }
                continue;
            }
        }
        files.push_back(file);
    }
    if (!problems.empty()) {
        error = "The pack was not brought in: ";
        for (std::size_t i = 0; i < problems.size() && i < 5; ++i) {
            error += (i == 0 ? "" : "; ") + problems[i];
        }
        error +=
            problems.size() > 5 ? "; and " + std::to_string(problems.size() - 5) + " more." : ".";
        return std::nullopt;
    }
    for (const std::string& file : files) {
        const fs::path from = pack / "assets" / native(file);
        const fs::path to = project / "assets" / native(file);
        std::error_code failed;
        fs::create_directories(to.parent_path(), failed);
        if (is_text(file) && !same_as.empty()) {
            std::string text_in = read_file(from).value_or(std::string());
            for (const auto& [theirs, ours] : same_as) {
                for (std::size_t at = text_in.find(theirs); at != std::string::npos;
                     at = text_in.find(theirs, at + ours.size())) {
                    text_in.replace(at, theirs.size(), ours);
                }
            }
            if (!write_file(to, text_in)) {
                error = "Bringing the pack in stopped at " + file + ".";
                return std::nullopt;
            }
        } else {
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, failed);
        }
        fs::path meta_from = from;
        meta_from += native(meta);
        fs::path meta_to = to;
        meta_to += native(meta);
        if (!failed && fs::exists(meta_from, failed)) {
            fs::copy_file(meta_from, meta_to, fs::copy_options::overwrite_existing, failed);
        }
        if (failed) {
            error = "Bringing the pack in stopped at " + file + ": " + failed.message();
            return std::nullopt;
        }
        result.added.push_back(file);
    }
    return result;
}

} // namespace editor
