#pragma once

// Asset packs (plan/02 A12), as Unity's packages carry assets between projects: pictures,
// sounds, prefabs, scenes, logic and conversations, chosen in one project with everything
// they use, and brought into another with their GUIDs, so their references keep working.
//
// A pack is a folder, "<name>.sparxelpack", holding "pack.json" (its name and files) and
// "assets/" with the files and their ".meta", as they sit in a project.
//
// See docs/dev/editor.md (The Assets panel, Asset packs).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

/// The pack file's format name and version.
inline constexpr std::string_view pack_format = "sparxel.pack";
inline constexpr int pack_version = 1;

/// Everything a pack of `chosen` (files or folders, relative to the project's assets/)
/// carries: those files, each with its group (a picture's sheet and Sprite Studio source),
/// and every asset they use, found by GUID or by path in their text, and what those use,
/// in turn. Sorted, relative to assets/. Paths that are not assets are left out.
[[nodiscard]] std::vector<std::string> pack_contents(const std::filesystem::path& project,
                                                     const std::vector<std::string>& chosen);

/// Writes a pack named `name` of `chosen` (with pack_contents()) to the folder `folder`,
/// which must not exist or be empty. Returns the files packed; nothing, with `error`, if
/// there is nothing to pack or it cannot be written.
std::optional<std::vector<std::string>> export_pack(const std::filesystem::path& project,
                                                    const std::vector<std::string>& chosen,
                                                    const std::filesystem::path& folder,
                                                    std::string_view name, std::string& error);

/// What bringing a pack in did.
struct PackImport {
    std::string name;                 ///< The pack's.
    std::vector<std::string> added;   ///< Copied into the project.
    std::vector<std::string> already; ///< The same file was there already: kept.
};

/// Brings the pack in the folder `pack` into the project, each file to the same place in
/// its assets/, with its ".meta". A file that is there already, the same, is kept, and what
/// the pack brings uses the project's GUID for it (two projects from one template have the
/// same tiles under different GUIDs). A picture or sound the project has moved elsewhere
/// (its GUID) is kept there. Refused, with `error` naming the files and nothing copied, if
/// a different file is where one goes, or a scene, logic or other file named by its path is
/// in the project in another place.
std::optional<PackImport> import_pack(const std::filesystem::path& project,
                                      const std::filesystem::path& pack, std::string& error);

} // namespace editor
