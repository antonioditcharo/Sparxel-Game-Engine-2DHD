#pragma once

// Versions of a project kept with Git (plan/02 A8), as Godot's Git plugin and Unity's
// version control window do: what changed since the last version, saving a version with a
// few words, and the history. The editor runs the git program; this reads what it prints.
//
// See docs/dev/editor.md, *Versions*.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace editor {

/// A file changed since the last version.
struct VersionChange {
    enum class Kind : std::uint8_t { New, Changed, Deleted, Renamed, Conflict };
    Kind kind = Kind::Changed;
    std::string path; ///< From the project's folder, with forward slashes.
    friend bool operator==(const VersionChange&, const VersionChange&) = default;
};

/// A version saved: a Git commit.
struct Version {
    std::string id;      ///< Short, as Git shows it: "a1b2c3d".
    std::string author;  ///< Who saved it.
    std::string when;    ///< In words: "3 minutes ago".
    std::string message; ///< What changed, in their words.
    friend bool operator==(const Version&, const Version&) = default;
};

/// The changes in `git status --porcelain=v1 -z` output, in its order: new (untracked or
/// added), changed, deleted, renamed (the new name), or in conflict after a merge.
[[nodiscard]] std::vector<VersionChange> parse_git_status(std::string_view porcelain);

/// The format the history is asked in (git log's --pretty), read by parse_git_log().
inline constexpr std::string_view git_log_format = "--pretty=format:%h%x1f%an%x1f%ar%x1f%s%x1e";
/// The versions in `git log` output made with git_log_format, the newest first.
[[nodiscard]] std::vector<Version> parse_git_log(std::string_view text);

/// What a project's .gitignore keeps out of its versions: the editor's own working files
/// (autosaves, snapshots, test saves) and exported games.
[[nodiscard]] std::string project_gitignore();

/// A change in a word or two: "New", "Changed", "Deleted", "Renamed", "In conflict".
[[nodiscard]] std::string_view change_word(VersionChange::Kind kind) noexcept;

} // namespace editor
